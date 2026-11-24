#!/usr/bin/env python3
"""Qualify one exact release-candidate package against the REL matrix.

Roadmap REL-01 through REL-04 need every functional, lifecycle and visual result
to come from the same package, not from tests that ran on different revisions or
on a staged tree that changed between them. This runner makes that the default:

* ``freeze`` hashes every file of a candidate runtime (an extracted release
  archive, or a staged tree) and records its version line, its source commit,
  the retail packs it will run against and the host. With ``--package`` it also
  proves the runtime holds exactly that archive's files. A dev or nightly
  version line names its commit; a clean stable release line names none, so
  ``--source-commit`` supplies it and the client's Windows version resource must
  carry that commit's history length as its build field. The record is
  ``candidate.json`` in the evidence directory.
* ``run`` executes the matrix cases one at a time against that runtime. Before
  and after every case it checks the runtime still matches the frozen manifest,
  and it refuses to continue on a changed runtime: a case that ran on other
  bytes is not evidence for this candidate. Every attempt, passed or failed, is
  appended to ``results.jsonl`` with its harness identity, exit code, duration,
  output directory and any crash report the runtime wrote. Nothing is
  overwritten, so a failure stays on record after a rerun passes.
* ``report`` writes ``summary.md``: the candidate identity, then each REL row
  with its latest result and attempt history, then the cases that did not run.

The cases are the existing input-free harnesses, pointed at the candidate. They
launch hidden windowed clients, bind loopback only and use engine commands and
render-target captures; none of them synthesises device input or captures the
desktop. Cases that need people or physical devices (REL-02's motion, latency,
audible sound, accessibility and VR comfort reviews) are listed as ``human``:
``run`` never marks them passed, and ``record-human`` stores the reviewer's
verdict with their own notes.

  release_qualification.py freeze --runtime .tmp/runtime/candidate \\
      --output .tmp/rel/evidence --basepath .tmp/rel/q4-packs \\
      --previous-release .tmp/runtime/release-0150 --awakening .home --source-root . \\
      [--package <release zip>] [--source-commit <full SHA, for a stable build>]
  release_qualification.py run --output .tmp/rel/evidence [--case ID ...] [--rerun-failed]
  release_qualification.py report --output .tmp/rel/evidence
  release_qualification.py list
  release_qualification.py self-test

See docs/dev/release-qualification.md.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zipfile
import zlib
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable

ROOT = Path(__file__).resolve().parents[2]
SCHEMA_VERSION = 1
CANDIDATE_KIND = "openq4-release-candidate"
RESULT_KIND = "openq4-release-qualification-attempt"

# Directories a run legitimately writes inside a runtime: crash reports and the
# per-user data a misconfigured harness might put beside the binaries. They are
# not package content, so the manifest leaves them out; crashes are reported.
MUTABLE_DIRECTORIES = ("crashes", "logs", "savegames", "screenshots", "demos")
BINARY_SUFFIXES = (".exe", ".dll", ".so", ".dylib", "")
# `openQ4 <version>[+g<sha>[.dirty]]`: a clean stable release line carries no build metadata.
VERSION_PATTERN = re.compile(rb"openQ4 (\d+\.\d+\.\d+(?:-[0-9A-Za-z.\-]*[0-9A-Za-z])?(?:\+[0-9A-Za-z.]*[0-9A-Za-z])?)")
HEX40 = re.compile(r"[0-9a-f]{40}")


@dataclass(frozen=True)
class Case:
    """One matrix cell: a harness command, the REL row it qualifies and its needs."""

    id: str
    task: str
    row: str
    summary: str
    command: tuple[str, ...] = ()
    timeout: int = 900
    needs: tuple[str, ...] = ()
    kind: str = "automated"  # automated | human
    renderer: str = ""
    harness: str = ""
    procedure: str = ""
    env: tuple[tuple[str, str], ...] = field(default_factory=tuple)
    # "stock-runtime": the harness accepts only .install or a named directory
    # below .tmp/stock-runtime, so it runs on a hard-linked view of the candidate.
    view: str = ""


def tool(name: str) -> str:
    return f"{{root}}/tools/tests/{name}"


# Placeholders: {python} {root} {runtime} {basepath} {awakening} {previous} {out}
# {client} {ded}. A case whose needs are not met is recorded as not run.
MATRIX: tuple[Case, ...] = (
    # REL-01: package identity, roles and stock-asset gameplay.
    Case("rel01.package.layout", "REL-01", "Package identity and layout",
         "The package carries exactly one engine, both renderer modules and both game modules for its "
         "architecture, the pk4s their build expects, licences and docs; no linker artefacts or stale modules.",
         ("{python}", "{root}/tools/validation/release_qualification.py", "check-layout", "--runtime", "{runtime}",
          "--output", "{out}"), timeout=300, harness="tools/validation/release_qualification.py"),
    Case("rel01.ded.startup", "REL-01", "Dedicated server role",
         "Asset-free dedicated server start, IPv4/IPv6 loopback self-test and clean exit.",
         ("{python}", tool("windows_dedicated_server_smoke.py"), "--install-root", "{runtime}",
          "--output-root", "{out}", "--timeout", "90"), timeout=300,
         harness="tools/tests/windows_dedicated_server_smoke.py"),
    Case("rel01.mp.dedicated-client", "REL-01", "MP client against a dedicated server",
         "LAN discovery and three successive joins of a pure dedicated server on IPv4 loopback, "
         "each reaching active gameplay on a stock map.",
         ("{python}", tool("mp_lan_reconnect_smoke.py"), "--runtime-dir", "{runtime}", "--basepath", "{basepath}",
          "--output-dir", "{out}", "--family", "ipv4"), timeout=1200, needs=("basepath",),
         harness="tools/tests/mp_lan_reconnect_smoke.py"),
    Case("rel01.mp.listen-gl", "REL-01", "MP listen server and client, OpenGL",
         "Loopback listen servers host bot matches on every stock MP map and draw gameplay.",
         ("{python}", tool("renderer_vulkan_qualification.py"), "run", "--suite", "mp-sweep", "--runtime-dir",
          "{runtime}", "--basepath", "{basepath}", "--output-dir", "{out}", "--source-commit", "{commit}", "--config", "gl=gl"),
         timeout=5400, needs=("basepath", "clean-source"), renderer="gl",
         harness="tools/tests/renderer_vulkan_qualification.py"),
    Case("rel01.mp.listen-vk", "REL-01", "MP listen server and client, Vulkan",
         "The same loopback bot matches on Vulkan, compared with an OpenGL reference.",
         ("{python}", tool("renderer_vulkan_qualification.py"), "run", "--suite", "mp-sweep", "--runtime-dir",
          "{runtime}", "--basepath", "{basepath}", "--output-dir", "{out}", "--source-commit", "{commit}", "--config", "vk=vulkan",
          "--config", "gl=gl", "--reference", "gl", "--contact-sheets"),
         timeout=9000, needs=("basepath", "clean-source"), renderer="vulkan",
         harness="tools/tests/renderer_vulkan_qualification.py"),
    Case("rel01.sp.stock-maps-gl", "REL-01", "SP stock campaign maps, OpenGL",
         "Every stock single-player map loads into gameplay and draws its opening view on OpenGL.",
         ("{python}", tool("renderer_vulkan_qualification.py"), "run", "--suite", "sp-sweep", "--runtime-dir",
          "{runtime}", "--basepath", "{basepath}", "--output-dir", "{out}", "--source-commit", "{commit}", "--config", "gl=gl"),
         timeout=7200, needs=("basepath", "clean-source"), renderer="gl",
         harness="tools/tests/renderer_vulkan_qualification.py"),
    Case("rel01.sp.stock-maps-vk", "REL-01", "SP stock campaign maps, Vulkan",
         "Every stock single-player map on Vulkan, with its warnings compared against OpenGL.",
         ("{python}", tool("renderer_vulkan_qualification.py"), "run", "--suite", "sp-sweep", "--runtime-dir",
          "{runtime}", "--basepath", "{basepath}", "--output-dir", "{out}", "--source-commit", "{commit}", "--config", "vk=vulkan",
          "--config", "gl=gl", "--reference", "gl", "--contact-sheets"),
         timeout=12000, needs=("basepath", "clean-source"), renderer="vulkan",
         harness="tools/tests/renderer_vulkan_qualification.py"),
    Case("rel01.sp.save-load", "REL-01", "SP save and load",
         "A fresh airdefense1 game saves after play; a second process restores it, resaves before and after "
         "time resumes, and every save is portable v5 with no unresolved references.",
         ("{python}", "{root}/tools/tests/release_profile_runtime.py", "save-roundtrip", "--runtime", "{runtime}",
          "--basepath", "{basepath}", "--output", "{out}"), timeout=1800, needs=("basepath",),
         harness="tools/tests/release_profile_runtime.py"),
    Case("rel01.sp.demo", "REL-01", "SP demo record and playback",
         "A demo recorded in airdefense1 gameplay plays back with timeDemoQuit in a fresh process, which renders "
         "every frame and exits cleanly.",
         ("{python}", "{root}/tools/tests/release_profile_runtime.py", "demo", "--runtime", "{runtime}",
          "--basepath", "{basepath}", "--output", "{out}"), timeout=1200, needs=("basepath",),
         harness="tools/tests/release_profile_runtime.py"),
    Case("rel01.sp.campaign-transitions", "REL-01", "SP campaign map transitions",
         "One client walks all 31 stock stages from airdefense1 to core2: on each map it saves, loads that save "
         "and fires the map's own end-level target, so the session performs the real level change with the "
         "player's persistent state, on the shipped physics default.",
         ("{python}", tool("physics_campaign.py"), "--exe", "{client}", "--retail", "{basepath}", "--save-root",
          "{out}/saves", "--parts", "chain", "--mode", "legacy", "--output", "{out}/campaign-chain.json"), timeout=9000,
         needs=("basepath",), harness="tools/tests/physics_campaign.py"),
    Case("rel01.sp.campaign-saves", "REL-01", "SP saves across campaign maps",
         "Saves made on five stock campaign maps reload into the same map in a fresh process.",
         ("{python}", tool("campaign_runtime.py"), "--retail", "{basepath}", "--exe", "{client}", "--save-root",
          "{out}/saves", "--game", "baseoq4", "--phase", "candidate"), timeout=3600, needs=("basepath",),
         harness="tools/tests/campaign_runtime.py"),
    Case("rel01.campaign.switch", "REL-01", "Campaign and mode switching",
         "Quake 4 and The Awakening are discovered, switched in place and captured through the engine.",
         ("{python}", tool("campaign_selection_runtime.py"), "--retail", "{basepath}", "--awakening", "{awakening}",
          "--runtime", "{runtime}", "--output", "{out}"), timeout=2400, needs=("basepath", "awakening"),
         harness="tools/tests/campaign_selection_runtime.py"),
    Case("rel01.install.clean", "REL-01", "Clean install",
         "A first launch with no profile detects a preset silently, writes its config, reaches gameplay and quits "
         "without errors; the second launch reads that config back unchanged.",
         ("{python}", "{root}/tools/tests/release_profile_runtime.py", "clean-install", "--runtime", "{runtime}",
          "--basepath", "{basepath}", "--output", "{out}"), timeout=1200, needs=("basepath",),
         harness="tools/tests/release_profile_runtime.py"),
    Case("rel01.install.upgrade", "REL-01", "Upgrade over the previous release's profile",
         "A profile made by the previous release (archived CVars, bindings, a save and a mod selection) keeps "
         "every player choice under the candidate, and the old save restores into gameplay.",
         ("{python}", "{root}/tools/tests/release_profile_runtime.py", "upgrade", "--runtime", "{runtime}",
          "--previous", "{previous}", "--basepath", "{basepath}", "--awakening", "{awakening}", "--output", "{out}"),
         timeout=2400, needs=("basepath", "previous", "awakening"), harness="tools/tests/release_profile_runtime.py"),
    Case("rel01.content-packs", "REL-01", "Pack mismatch reporting",
         "A missing or stale client pack stops startup with a localized report naming the pack; a stale core "
         "pack copy in a mod folder is skipped.",
         ("{python}", tool("content_pack_status_smoke.py"), "--runtime-dir", "{runtime}", "--basepath", "{basepath}",
          "--output-dir", "{out}"), timeout=1200, needs=("basepath",),
         harness="tools/tests/content_pack_status_smoke.py"),
    Case("rel01.localization", "REL-01", "Localized startup",
         "A complete non-English language table drives the SP front end and gameplay text.",
         ("{python}", tool("localization_smoke.py"), "--basepath", "{basepath}", "--runtime-dir", "{runtime}",
          "--output-dir", "{out}"), timeout=900, needs=("basepath",),
         harness="tools/tests/localization_smoke.py"),
    # REL-03: lifecycle and failure behaviour of the same package.
    Case("rel03.soak-gl", "REL-03", "Long session and map changes, OpenGL",
         "One process tours a ring of SP maps for several cycles with a vid_restart, with process memory sampled "
         "at every stop; growth must stay bounded.",
         ("{python}", tool("renderer_vulkan_qualification.py"), "run", "--suite", "soak", "--runtime-dir",
          "{runtime}", "--basepath", "{basepath}", "--output-dir", "{out}", "--source-commit", "{commit}", "--config", "gl=gl"),
         timeout=7200, needs=("basepath", "clean-source"), renderer="gl",
         harness="tools/tests/renderer_vulkan_qualification.py"),
    Case("rel03.soak-vk", "REL-03", "Long session and map changes, Vulkan",
         "The same soak on Vulkan, also sampling the Vulkan heaps.",
         ("{python}", tool("renderer_vulkan_qualification.py"), "run", "--suite", "soak", "--runtime-dir",
          "{runtime}", "--basepath", "{basepath}", "--output-dir", "{out}", "--source-commit", "{commit}", "--config", "vk=vulkan",
          "--no-validation"),
         timeout=7200, needs=("basepath", "clean-source"), renderer="vulkan",
         harness="tools/tests/renderer_vulkan_qualification.py"),
    Case("rel03.lifecycle", "REL-03", "Cold and warm starts, cache loss, resize and shutdown",
         "Cold start on an empty profile, warm start on its caches, start after the generated cache is "
         "corrupted, a live window resize and renderer restart in gameplay, pause and slow motion, and clean "
         "shutdown with exit code 0 and no crash report.",
         ("{python}", "{root}/tools/tests/release_profile_runtime.py", "lifecycle", "--runtime", "{runtime}",
          "--basepath", "{basepath}", "--output", "{out}"), timeout=2400, needs=("basepath",),
         harness="tools/tests/release_profile_runtime.py"),
    Case("rel03.network.reconnect-impaired", "REL-03", "Network loss and reconnect",
         "Joins, disconnects and reconnects to a pure dedicated server through simulated loss and latency.",
         ("{python}", tool("mp_lan_reconnect_smoke.py"), "--runtime-dir", "{runtime}", "--basepath", "{basepath}",
          "--output-dir", "{out}", "--family", "ipv4", "--impaired"), timeout=1800, needs=("basepath",),
         harness="tools/tests/mp_lan_reconnect_smoke.py"),
    Case("rel03.network.ipv6", "REL-03", "IPv6 loopback reconnect",
         "The same joins over IPv6 loopback.",
         ("{python}", tool("mp_lan_reconnect_smoke.py"), "--runtime-dir", "{runtime}", "--basepath", "{basepath}",
          "--output-dir", "{out}", "--family", "ipv6-loopback"), timeout=1200, needs=("basepath",),
         harness="tools/tests/mp_lan_reconnect_smoke.py"),
    Case("rel03.renderer.vk-recovery", "REL-03", "Renderer recovery",
         "Same-process Vulkan startup recovery followed by stock-map gameplay.",
         ("{python}", tool("renderer_vulkan_recovery_gameplay.py"), "--runtime-dir", "{runtime}", "--basepath",
          "{basepath}", "--output-dir", "{out}"), timeout=2400, needs=("basepath",), renderer="vulkan",
         harness="tools/tests/renderer_vulkan_recovery_gameplay.py", view="stock-runtime"),
    Case("rel03.audio.recovery", "REL-03", "Audio output loss and recovery",
         "Rendered through OpenAL Soft's wave writer: the output is interrupted while tones, a paused line and Ogg "
         "streams play, audio memory is trimmed and a purged sample replays; then the speaker layout and HRTF "
         "change during dialogue and the device resets in place.",
         ("{python}", tool("audio_output_recovery_runtime.py"), "--runtime-dir", "{runtime}", "--assets", "{basepath}",
          "--output", "{out}"), timeout=1800, needs=("basepath",),
         harness="tools/tests/audio_output_recovery_runtime.py"),
    Case("rel03.effects.lifecycle", "REL-03", "Effect runtime across map changes and restores",
         "BSE effects survive map changes, save restores and renderer restarts without leaks.",
         ("{python}", tool("bse_lifecycle_runtime.py"), "--install-root", "{runtime}", "--basepath", "{basepath}",
          "--output", "{out}"), timeout=1200, needs=("basepath",),
         harness="tools/tests/bse_lifecycle_runtime.py"),
    # REL-04: the package checks a promotion relies on.
    Case("rel04.package.symbols", "REL-04", "Release symbols match the binaries",
         "Every openQ4 image in the runtime has a PDB in the release's symbols archive whose GUID matches the "
         "image's CodeView record, so a crash in any of them can be symbolized.",
         ("{python}", "{root}/tools/validation/release_qualification.py", "check-symbols", "--runtime", "{runtime}",
          "--symbols", "{symbols}", "--output", "{out}"), timeout=600, needs=("symbols",),
         harness="tools/validation/release_qualification.py"),
    Case("rel04.package.signing", "REL-04", "Code signing status",
         "Records each Windows image's Authenticode status. Windows packages are unsigned; this case states it "
         "and fails only if an image claims a signature that does not verify.",
         ("{python}", "{root}/tools/validation/release_qualification.py", "check-signing", "--runtime", "{runtime}",
          "--output", "{out}"), timeout=600, harness="tools/validation/release_qualification.py"),
    # REL-02: engine captures the matrix can take, and the reviews it cannot.
    Case("rel02.captures.generated-images", "REL-02", "Engine render-target captures",
         "Generated images and front-end captures through the engine's screenshot command on both renderers.",
         ("{python}", tool("ui_generated_image_runtime.py"), "--runtime-dir", "{runtime}", "--basepath", "{basepath}",
          "--output-dir", "{out}"), timeout=900, needs=("basepath",),
         harness="tools/tests/ui_generated_image_runtime.py"),
    Case("rel02.human.visual", "REL-02", "Matched visual acceptance", "", kind="human",
         procedure="Review the contact sheets rel01.sp.stock-maps-vk and rel01.mp.listen-vk wrote: every stock map's "
                   "opening view on Vulkan beside OpenGL at the same viewpoint and game time. Accept or list each "
                   "visible difference (missing surfaces, lighting, effects, HUD) with its map."),
    Case("rel02.human.motion", "REL-02", "Motion and frame pacing review", "", kind="human",
         procedure="Play airdefense1 and hub1 for five minutes each on GL and Vulkan, windowed and (with your "
                   "permission) fullscreen at the display's refresh: judge camera, weapon and enemy motion for "
                   "judder, hitches and tearing."),
    Case("rel02.human.latency", "REL-02", "Input latency review", "", kind="human",
         procedure="With a mouse and a controller, compare turn and fire responsiveness at 60 Hz and the display's "
                   "refresh, with and without vsync; or run presentation_reference_suite.py --reference with a "
                   "click-to-photon sensor."),
    Case("rel02.human.audio", "REL-02", "Audible sound review", "", kind="human",
         procedure="On speakers or headphones: weapon fire, explosions, dialogue with subtitles, music, ambience and "
                   "reverb in airdefense1 and a multiplayer match; unplug and replug the output device in gameplay."),
    Case("rel02.human.accessibility", "REL-02", "Accessibility review", "", kind="human",
         procedure="Subtitles and speaker names, text scale, reduced motion, controller-only menu navigation and the "
                   "on-screen keyboard, colour and contrast of HUD and menus."),
    Case("rel02.human.vr", "REL-02", "VR comfort review", "", kind="human",
         procedure="On a physical OpenXR headset: docs/dev/vr-headset-qualification.md, including comfort options, "
                   "snap and smooth turning, seated and standing play spaces and a full level."),
)


# ---------------------------------------------------------------------------
# Identity


def sha256_file(path: Path, chunk: int = 1 << 20) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            block = handle.read(chunk)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def git(root: Path, *args: str) -> str | None:
    try:
        result = subprocess.run(["git", "-C", str(root), *args], capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.SubprocessError):
        return None
    return result.stdout.strip() if result.returncode == 0 else None


def host_platform() -> str:
    system = {"darwin": "macos"}.get(platform.system().lower(), platform.system().lower())
    machine = platform.machine().lower()
    arch = {"amd64": "x64", "x86_64": "x64", "arm64": "arm64", "aarch64": "arm64"}.get(machine, machine)
    return f"{system}-{arch}"


def client_name(arch: str = "x64") -> str:
    return f"openQ4-client_{arch}{'.exe' if sys.platform == 'win32' else ''}"


def ded_name(arch: str = "x64") -> str:
    return f"openQ4-ded_{arch}{'.exe' if sys.platform == 'win32' else ''}"


def is_mutable(relative: Path) -> bool:
    return bool(relative.parts) and relative.parts[0].casefold() in MUTABLE_DIRECTORIES


def runtime_files(runtime: Path) -> Iterable[tuple[str, Path]]:
    for path in sorted(runtime.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        relative = path.relative_to(runtime)
        if is_mutable(relative):
            continue
        yield relative.as_posix(), path


def hash_manifest(runtime: Path) -> dict[str, dict[str, Any]]:
    manifest: dict[str, dict[str, Any]] = {}
    for name, path in runtime_files(runtime):
        stat = path.stat()
        manifest[name] = {"size": stat.st_size, "mtimeNs": stat.st_mtime_ns, "sha256": sha256_file(path)}
    return manifest


def manifest_digest(manifest: dict[str, dict[str, Any]]) -> str:
    digest = hashlib.sha256()
    for name in sorted(manifest):
        digest.update(f"{name}\0{manifest[name]['size']}\0{manifest[name]['sha256']}\n".encode("utf-8"))
    return digest.hexdigest()


def version_line(executable: Path) -> dict[str, Any] | None:
    """The `openQ4 <version>+g<sha>` string compiled into an engine binary."""
    if not executable.is_file():
        return None
    data = executable.read_bytes()
    matches = sorted({m.group(1).decode("ascii") for m in VERSION_PATTERN.finditer(data)}, key=len, reverse=True)
    if not matches:
        return None
    version = matches[0]
    metadata = version.partition("+")[2].split(".")
    sha = next((part[1:] for part in metadata if re.fullmatch(r"g[0-9a-f]{7,40}", part)), None)
    return {"version": version, "commit": sha, "dirty": "dirty" in metadata}


def resource_version(executable: Path) -> tuple[int, int, int, int] | None:
    """FILEVERSION of a Windows image; openQ4 puts the source commit count in its fourth field."""
    if not executable.is_file():
        return None
    data = executable.read_bytes()
    offset = data.find(struct.pack("<I", 0xFEEF04BD))
    if offset < 0 or offset + 16 > len(data):
        return None
    _, _, high, low = struct.unpack_from("<4I", data, offset)
    return high >> 16, high & 0xFFFF, low >> 16, low & 0xFFFF


def bind_source_commit(engine: dict[str, Any] | None, executable: Path, source_root: Path,
                       explicit: str | None) -> tuple[str | None, dict[str, Any]]:
    """The full commit a runtime was built from, and how that link was established.

    A dev or nightly version line names its commit. A clean stable release line
    does not (openq4_version.py), so the operator names it and the image's
    version resource must carry that commit's history length as its build field.
    """
    binding: dict[str, Any] = {"method": None}
    if engine and engine.get("commit"):
        commit = git(source_root, "rev-parse", "--verify", "--quiet", f"{engine['commit']}^{{commit}}")
        if explicit and commit and explicit.lower() != commit:
            binding["error"] = f"--source-commit {explicit} is not the version line's g{engine['commit']}"
            return None, binding
        binding["method"] = "version line"
        return (commit if commit and HEX40.fullmatch(commit) else None), binding
    if not explicit:
        binding["error"] = "the version line names no commit; pass --source-commit"
        return None, binding
    commit = git(source_root, "rev-parse", "--verify", "--quiet", f"{explicit}^{{commit}}")
    if not commit or not HEX40.fullmatch(commit):
        binding["error"] = f"--source-commit {explicit} does not resolve in {source_root}"
        return None, binding
    count = git(source_root, "rev-list", "--count", commit)
    resource = resource_version(executable)
    binding.update(method="operator commit, checked against the version resource build", commitCount=count,
                   resourceVersion=list(resource) if resource else None)
    if resource is None or count is None or int(count) != resource[3]:
        binding["error"] = f"the version resource build {resource[3] if resource else None} is not {commit}'s "                            f"history length {count}"
        return None, binding
    return commit, binding


def check_runtime(runtime: Path, frozen: dict[str, dict[str, Any]], deep: bool) -> list[str]:
    """Differences between a runtime and its frozen manifest; hashes binaries always, pk4s when deep."""
    problems: list[str] = []
    seen: set[str] = set()
    for name, path in runtime_files(runtime):
        seen.add(name)
        expected = frozen.get(name)
        if expected is None:
            problems.append(f"added {name}")
            continue
        stat = path.stat()
        if stat.st_size != expected["size"]:
            problems.append(f"size changed {name}")
            continue
        suffix = path.suffix.lower()
        rehash = deep or stat.st_mtime_ns != expected["mtimeNs"] or suffix in (".exe", ".dll", ".so", ".dylib") \
            or (not suffix and os.access(path, os.X_OK))
        if rehash and sha256_file(path) != expected["sha256"]:
            problems.append(f"content changed {name}")
    problems += [f"removed {name}" for name in sorted(set(frozen) - seen)]
    return problems


def crash_reports(runtime: Path) -> list[str]:
    folder = runtime / "crashes"
    if not folder.is_dir():
        return []
    return sorted(p.name for p in folder.iterdir() if p.is_file())


def crc32_file(path: Path, chunk: int = 1 << 20) -> int:
    value = 0
    with path.open("rb") as handle:
        while True:
            block = handle.read(chunk)
            if not block:
                return value & 0xFFFFFFFF
            value = zlib.crc32(block, value)


def compare_with_archive(runtime: Path, archive: Path) -> dict[str, Any]:
    """Whether a runtime holds exactly a zip archive's files (one top-level folder is stripped)."""
    with zipfile.ZipFile(archive) as package:
        entries = [info for info in package.infolist() if not info.is_dir()]
    tops = {info.filename.split("/", 1)[0] for info in entries}
    prefix = (tops.pop() + "/") if len(tops) == 1 and all("/" in info.filename for info in entries) else ""
    mismatched, missing = [], []
    names = set()
    for info in entries:
        name = info.filename[len(prefix):]
        names.add(name)
        path = runtime / name
        if not path.is_file():
            missing.append(name)
        elif path.stat().st_size != info.file_size or crc32_file(path) != info.CRC:
            mismatched.append(name)
    extra = sorted(name for name, _ in runtime_files(runtime) if name not in names)
    return {"entries": len(entries), "prefix": prefix.rstrip("/"), "missing": missing[:50],
            "mismatched": mismatched[:50], "notInPackage": extra[:50],
            "matches": not missing and not mismatched}


def host_identity() -> dict[str, Any]:
    identity: dict[str, Any] = {"platform": host_platform(), "os": platform.platform(), "python": platform.python_version()}
    if sys.platform == "win32":
        script = ("$g=Get-CimInstance Win32_VideoController|Select-Object Name,DriverVersion;"
                  "$c=(Get-CimInstance Win32_Processor|Select-Object -First 1).Name;"
                  "$m=(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory;"
                  "@{gpus=@($g);cpu=$c;memoryBytes=$m}|ConvertTo-Json -Depth 3 -Compress")
        try:
            output = subprocess.run(["powershell", "-NoProfile", "-Command", script], capture_output=True, text=True,
                                    timeout=120).stdout
            identity.update(json.loads(output))
        except (OSError, subprocess.SubprocessError, ValueError):
            pass
    return identity


def retail_identity(basepath: Path) -> dict[str, Any]:
    packs = basepath / "q4base"
    entries = {}
    for path in sorted(packs.glob("*.pk4")) if packs.is_dir() else []:
        entries[path.name] = {"size": path.stat().st_size, "sha256": sha256_file(path)}
    loose = [p.relative_to(packs).as_posix() for p in packs.rglob("*") if p.is_file() and p.suffix.lower() != ".pk4"] \
        if packs.is_dir() else []
    return {"basepath": str(basepath), "packs": entries, "looseFiles": len(loose), "looseSample": loose[:20]}


# ---------------------------------------------------------------------------
# Commands


def evidence_paths(output: Path) -> tuple[Path, Path, Path]:
    return output / "candidate.json", output / "results.jsonl", output / "summary.md"


def load_candidate(output: Path) -> dict[str, Any]:
    path = evidence_paths(output)[0]
    if not path.is_file():
        raise SystemExit(f"no candidate record at {path}; run freeze first")
    return json.loads(path.read_text(encoding="utf-8"))


def command_freeze(args: argparse.Namespace) -> int:
    runtime = args.runtime.resolve()
    output = args.output.resolve()
    candidate_path = evidence_paths(output)[0]
    if candidate_path.exists() and not args.force:
        raise SystemExit(f"{candidate_path} exists: a candidate is frozen once; use a new --output or --force")
    if not (runtime / client_name()).is_file():
        raise SystemExit(f"no {client_name()} in {runtime}")
    output.mkdir(parents=True, exist_ok=True)
    print(f"hashing {runtime} ...", flush=True)
    manifest = hash_manifest(runtime)
    engine = version_line(runtime / client_name())
    dedicated = version_line(runtime / ded_name())
    source_root = args.source_root.resolve()
    commit, binding = bind_source_commit(engine, runtime / client_name(), source_root, args.source_commit)
    record: dict[str, Any] = {
        "kind": CANDIDATE_KIND,
        "schemaVersion": SCHEMA_VERSION,
        "frozen": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "label": args.label or (engine["version"] if engine else runtime.name),
        "runtime": str(runtime),
        "platform": host_platform(),
        "engine": engine,
        "dedicated": dedicated,
        "sourceCommit": commit,
        "sourceBinding": binding,
        "sourceRoot": str(source_root),
        "package": None,
        "manifestSha256": manifest_digest(manifest),
        "files": manifest,
        "inputs": {
            "basepath": str(args.basepath.resolve()) if args.basepath else None,
            "awakening": str(args.awakening.resolve()) if args.awakening else None,
            "previous": str(args.previous_release.resolve()) if args.previous_release else None,
        },
        "retail": retail_identity(args.basepath.resolve()) if args.basepath else None,
        "previousRelease": None,
        "host": host_identity() if getattr(args, "host", True) else None,
        "notes": args.note or [],
    }
    if args.package:
        package = args.package.resolve()
        record["package"] = {"path": str(package), "size": package.stat().st_size, "sha256": sha256_file(package),
                             "runtimeComparison": compare_with_archive(runtime, package)}
    if args.previous_release:
        previous = args.previous_release.resolve()
        record["previousRelease"] = {"runtime": str(previous), "engine": version_line(previous / client_name()),
                                     "manifestSha256": manifest_digest(hash_manifest(previous))}
    candidate_path.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: record[key] for key in ("label", "engine", "sourceCommit", "manifestSha256")}, indent=2))
    problems = []
    if not engine:
        problems.append("the client carries no openQ4 version line")
    elif engine["dirty"]:
        problems.append("the client was built from a dirty tree: its source is not reproducible")
    if engine and not record["sourceCommit"]:
        problems.append(binding.get("error") or f"g{engine['commit']} does not resolve to a commit in {source_root}")
    if engine and dedicated and engine["version"] != dedicated["version"]:
        problems.append(f"client {engine['version']} and dedicated server {dedicated['version']} differ")
    if record["package"] and not record["package"]["runtimeComparison"]["matches"]:
        problems.append("the runtime does not hold exactly the package's files")
    for problem in problems:
        print(f"WARNING: {problem}")
    return 0 if not problems else 1


def needs_met(case: Case, candidate: dict[str, Any]) -> str | None:
    inputs = candidate.get("inputs", {})
    for need in case.needs:
        if need == "basepath" and not inputs.get("basepath"):
            return "no retail basepath was frozen with the candidate"
        if need == "awakening" and not inputs.get("awakening"):
            return "no Awakening content was frozen with the candidate"
        if need == "previous" and not inputs.get("previous"):
            return "no previous release runtime was frozen with the candidate"
        if need == "symbols" and not symbols_archive(candidate):
            return "no symbols archive beside the frozen package"
        if need == "clean-source":
            engine = candidate.get("engine") or {}
            if engine.get("dirty") or not candidate.get("sourceCommit"):
                return "the harness requires a clean, resolvable version-line commit"
    return None


STOCK_RUNTIME_PARENT = ROOT / ".tmp" / "stock-runtime"


def make_link_view(runtime: Path, view: Path) -> None:
    """A tree of hard links to the candidate's package files: the same bytes, no copy."""
    parent = STOCK_RUNTIME_PARENT.resolve()
    if view.resolve().parent != parent or view.is_symlink():
        raise SystemExit(f"refusing runtime view outside {parent}: {view}")
    remove_link_view(view)
    for name, source in runtime_files(runtime):
        target = view / name
        target.parent.mkdir(parents=True, exist_ok=True)
        os.link(source, target)


def remove_link_view(view: Path) -> None:
    if not view.exists():
        return
    if view.resolve().parent != STOCK_RUNTIME_PARENT.resolve() or view.is_symlink():
        raise SystemExit(f"refusing to remove {view}")
    shutil.rmtree(view)  # removes the links; the candidate's files keep their other name


def expand(case: Case, candidate: dict[str, Any], out: Path, runtime: Path | None = None) -> list[str]:
    inputs = candidate.get("inputs", {})
    runtime = runtime or Path(candidate["runtime"])
    values = {
        "python": sys.executable, "root": str(ROOT), "runtime": str(runtime),
        "basepath": inputs.get("basepath") or "", "awakening": inputs.get("awakening") or "",
        "previous": inputs.get("previous") or "", "out": str(out),
        "client": str(runtime / client_name()), "ded": str(runtime / ded_name()),
        "commit": candidate.get("sourceCommit") or "",
        "symbols": symbols_archive(candidate) or "",
    }
    return [part.format(**values) for part in case.command]


def harness_identity(case: Case, candidate: dict[str, Any]) -> dict[str, Any] | None:
    if not case.harness:
        return None
    path = ROOT / case.harness
    identity: dict[str, Any] = {"path": case.harness, "exists": path.is_file()}
    if path.is_file():
        identity["gitBlob"] = git(ROOT, "hash-object", str(path))
        commit = candidate.get("sourceCommit")
        committed = git(ROOT, "rev-parse", f"{commit}:{case.harness}") if commit else None
        identity["candidateBlob"] = committed
        identity["matchesCandidate"] = committed is not None and committed == identity["gitBlob"]
    return identity


def read_results(output: Path) -> list[dict[str, Any]]:
    path = evidence_paths(output)[1]
    if not path.is_file():
        return []
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def append_result(output: Path, record: dict[str, Any]) -> None:
    with evidence_paths(output)[1].open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(record, sort_keys=True) + "\n")


def free_commit_gb() -> float | None:
    if sys.platform != "win32":
        return None
    try:
        output = subprocess.run(["powershell", "-NoProfile", "-Command",
                                 "(Get-CimInstance Win32_OperatingSystem).FreeVirtualMemory"],
                                capture_output=True, text=True, timeout=60).stdout
        return int(output.strip()) / (1024 * 1024)
    except (OSError, subprocess.SubprocessError, ValueError):
        return None


def wait_for_memory(minimum: float, limit: float = 1800) -> float | None:
    if minimum <= 0:
        return None
    deadline = time.monotonic() + limit
    while True:
        free = free_commit_gb()
        if free is None or free >= minimum or time.monotonic() >= deadline:
            return free
        print(f"  waiting: {free:.1f} GB free commit, need {minimum}", flush=True)
        time.sleep(30)


def tail(path: Path, lines: int = 40) -> str:
    if not path.is_file():
        return ""
    return "\n".join(path.read_text(encoding="utf-8", errors="replace").splitlines()[-lines:])


def run_case(case: Case, candidate: dict[str, Any], output: Path, args: argparse.Namespace) -> dict[str, Any]:
    runtime = Path(candidate["runtime"])
    attempts = [r for r in read_results(output) if r["case"] == case.id]
    attempt = len(attempts) + 1
    case_dir = output / "cases" / case.id / f"attempt-{attempt}"
    record: dict[str, Any] = {
        "kind": RESULT_KIND, "case": case.id, "task": case.task, "row": case.row, "attempt": attempt,
        "candidate": candidate["manifestSha256"], "started": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "outputDir": str(case_dir), "harness": harness_identity(case, candidate), "note": args.note,
    }
    missing = needs_met(case, candidate)
    if missing:
        record.update(status="not-run", reason=missing, seconds=0)
        return record
    before = check_runtime(runtime, candidate["files"], deep=False)
    if before:
        record.update(status="blocked", reason="runtime differs from the frozen candidate", runtimeProblems=before[:20],
                      seconds=0)
        return record
    run_runtime = runtime
    view = None
    if case.view == "stock-runtime":
        view = STOCK_RUNTIME_PARENT / f"rel-{case.id}"
        make_link_view(runtime, view)
        run_runtime = view
        record["runtimeView"] = str(view)
    crashes_before = set(crash_reports(runtime)) | set(crash_reports(run_runtime))
    record["freeCommitGB"] = wait_for_memory(args.min_free_gb)
    case_dir.mkdir(parents=True, exist_ok=True)
    # Several harnesses insist on creating their own output directory.
    command = expand(case, candidate, case_dir / "output", run_runtime)
    record["command"] = command
    environment = dict(os.environ)
    # The candidate is frozen and checked by hash around every case, which is
    # stricter than the smokes' builddir-timestamp freshness guard; that guard
    # compares against this checkout's builddir, not the candidate's build.
    environment["OPENQ4_ALLOW_STALE_RUNTIME"] = "1"
    environment.update(dict(case.env))
    log_path = case_dir / "harness-output.txt"
    started = time.monotonic()
    timed_out = False
    with log_path.open("w", encoding="utf-8", errors="replace") as log:
        log.write("$ " + " ".join(command) + "\n\n")
        log.flush()
        process = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                                   env=environment)
        try:
            exit_code = process.wait(timeout=case.timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            process.kill()
            exit_code = process.wait(timeout=60)
    record["seconds"] = round(time.monotonic() - started, 1)
    record["exitCode"] = exit_code
    record["timedOut"] = timed_out
    new_crashes = sorted((set(crash_reports(runtime)) | set(crash_reports(run_runtime))) - crashes_before)
    if view is not None:
        remove_link_view(view)
    record["crashReports"] = new_crashes
    after = check_runtime(runtime, candidate["files"], deep=False)
    record["runtimeProblemsAfter"] = after[:20]
    if after:
        status = "invalid"
        record["reason"] = "the runtime changed during the case"
    elif timed_out:
        status = "fail"
        record["reason"] = f"timed out after {case.timeout} s"
    elif exit_code != 0 or new_crashes:
        status = "fail"
        record["reason"] = "crash report written" if new_crashes else f"exit code {exit_code}"
    else:
        status = "pass"
    record["status"] = status
    record["outputTail"] = tail(log_path)
    return record


def select_cases(args: argparse.Namespace) -> list[Case]:
    cases = [case for case in MATRIX if case.kind == "automated"]
    if args.task:
        cases = [case for case in cases if case.task in args.task]
    if args.case:
        known = {case.id for case in MATRIX}
        unknown = [name for name in args.case if name not in known]
        if unknown:
            raise SystemExit(f"unknown case(s): {', '.join(unknown)}")
        by_id = {case.id: case for case in cases}
        cases = [by_id[name] for name in dict.fromkeys(args.case) if name in by_id]
    return cases


def latest(results: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    by_case: dict[str, dict[str, Any]] = {}
    for result in results:
        by_case[result["case"]] = result
    return by_case


def command_run(args: argparse.Namespace) -> int:
    output = args.output.resolve()
    candidate = load_candidate(output)
    runtime = Path(candidate["runtime"])
    print(f"verifying {runtime} against the frozen manifest ...", flush=True)
    problems = check_runtime(runtime, candidate["files"], deep=True)
    if problems:
        raise SystemExit("the runtime no longer matches the frozen candidate:\n  " + "\n  ".join(problems[:20]))
    cases = select_cases(args)
    if args.rerun_failed:
        previous = latest(read_results(output))
        cases = [case for case in cases if previous.get(case.id, {}).get("status") not in ("pass",)]
    elif args.skip_passed:
        previous = latest(read_results(output))
        cases = [case for case in cases if previous.get(case.id, {}).get("status") != "pass"]
    failures = 0
    for case in cases:
        print(f"[{datetime.now().strftime('%H:%M:%S')}] {case.id}: {case.row}", flush=True)
        record = run_case(case, candidate, output, args)
        append_result(output, record)
        print(f"  -> {record['status']} ({record.get('seconds', 0)} s){': ' + record['reason'] if record.get('reason') else ''}",
              flush=True)
        if record["status"] != "pass":
            failures += 1
        if record["status"] in ("blocked", "invalid"):
            print("stopping: the candidate runtime changed", flush=True)
            break
    write_report(output)
    return 0 if failures == 0 else 1


def command_record_human(args: argparse.Namespace) -> int:
    output = args.output.resolve()
    candidate = load_candidate(output)
    case = next((c for c in MATRIX if c.id == args.case), None)
    if case is None or case.kind != "human":
        raise SystemExit(f"{args.case} is not a human review case")
    attempts = [r for r in read_results(output) if r["case"] == case.id]
    record = {
        "kind": RESULT_KIND, "case": case.id, "task": case.task, "row": case.row, "attempt": len(attempts) + 1,
        "candidate": candidate["manifestSha256"], "started": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "status": args.verdict, "reviewer": args.reviewer, "reason": args.notes, "seconds": 0, "human": True,
        "devices": args.device or [],
    }
    append_result(output, record)
    write_report(output)
    print(json.dumps(record, indent=2))
    return 0


def check_layout(runtime: Path) -> list[str]:
    """Release-package layout rules the platform archive verifiers also enforce, for any runtime root."""
    problems: list[str] = []
    arch = "x64"
    for name in (client_name(arch), ded_name(arch)):
        if not (runtime / name).is_file():
            problems.append(f"missing {name}")
    suffix = ".dll" if sys.platform == "win32" else (".dylib" if sys.platform == "darwin" else ".so")
    for name in (f"renderer-gl_{arch}{suffix}", f"renderer-vk_{arch}{suffix}"):
        if not (runtime / name).is_file():
            problems.append(f"missing {name}")
    base = runtime / "baseoq4"
    game_module = f"game_{arch}{suffix}"
    if not (base / game_module).is_file():
        problems.append(f"missing baseoq4/{game_module}")
    # game-* also catches the retired split game-sp/game-mp modules of an older build
    for module in sorted([*base.glob("game_*"), *base.glob("game-*")]) if base.is_dir() else []:
        if module.name != game_module and module.suffix == suffix:
            problems.append(f"stale or foreign game module baseoq4/{module.name}")
    for engine in sorted(runtime.glob("openQ4-*")):
        if engine.suffix.lower() in (".exe", "") and engine.is_file() and engine.name not in (
                client_name(arch), ded_name(arch), f"openQ4-updater_{arch}{'.exe' if sys.platform == 'win32' else ''}",
                "openQ4-steamdeck"):
            problems.append(f"unexpected root binary {engine.name}")
    if not sorted(base.glob("pak*.pk4")) if base.is_dir() else True:
        problems.append("no openQ4 pk4 in baseoq4")
    for stray in sorted(runtime.rglob("*")):
        if stray.is_file() and stray.suffix.lower() in (".lib", ".exp", ".ilk", ".obj", ".pdb"):
            problems.append(f"build artefact in the runtime package: {stray.relative_to(runtime).as_posix()}")
    if sys.platform == "win32" and not (runtime / "OpenAL32.dll").is_file():
        problems.append("missing OpenAL32.dll")
    for notice in ("LICENSE",):
        if not (runtime / notice).is_file():
            problems.append(f"missing {notice}")
    if sys.platform == "win32":
        machines = {"x64": 0x8664, "arm64": 0xAA64}
        for path in [runtime / client_name(arch), runtime / ded_name(arch), *runtime.glob(f"renderer-*_{arch}.dll"),
                     *base.glob(f"game_{arch}.dll"), runtime / "OpenAL32.dll"]:
            if path.is_file():
                data = path.read_bytes()[:4096]
                offset = int.from_bytes(data[0x3C:0x40], "little")
                machine = int.from_bytes(data[offset + 4:offset + 6], "little") if data[offset:offset + 4] == b"PE\0\0" \
                    else None
                if machine != machines[arch]:
                    problems.append(f"{path.relative_to(runtime).as_posix()} is not a {arch} PE image")
    engine = version_line(runtime / client_name(arch))
    dedicated = version_line(runtime / ded_name(arch))
    if engine and dedicated and engine["version"] != dedicated["version"]:
        problems.append(f"client {engine['version']} and dedicated server {dedicated['version']} differ")
    for module in [*runtime.glob(f"renderer-*_{arch}{suffix}"), *base.glob(f"game_{arch}{suffix}")]:
        stamp = version_line(module)
        if engine and stamp and stamp["commit"] != engine["commit"]:
            problems.append(f"{module.name} names g{stamp['commit']}, the engine g{engine['commit']}")
    return problems


def symbols_archive(candidate: dict[str, Any]) -> str | None:
    package = (candidate.get("package") or {}).get("path")
    if not package or not package.endswith(".zip"):
        return None
    symbols = Path(package[:-4] + "-symbols.zip")
    return str(symbols) if symbols.is_file() else None


def codeview_record(image: Path) -> tuple[bytes, int, str] | None:
    """GUID, age and PDB name from a PE image's RSDS CodeView record."""
    data = image.read_bytes()
    offset = data.rfind(b"RSDS")
    while offset >= 0:
        end = data.find(b"\0", offset + 24)
        if end > offset + 24:
            name = data[offset + 24:end].decode("latin-1", errors="replace")
            if name.lower().endswith(".pdb"):
                guid = data[offset + 4:offset + 20]
                age = int.from_bytes(data[offset + 20:offset + 24], "little")
                return guid, age, name
        offset = data.rfind(b"RSDS", 0, offset)
    return None


def openq4_images(runtime: Path) -> list[Path]:
    """The images openQ4 builds; bundled third-party libraries carry their own symbols, or none."""
    images = []
    for path in sorted(runtime.rglob("*")):
        if path.is_file() and path.suffix.lower() in (".exe", ".dll") and path.name.lower() != "openal32.dll":
            images.append(path)
    return images


def check_symbols(runtime: Path, symbols: Path) -> dict[str, Any]:
    with zipfile.ZipFile(symbols) as archive:
        entries = {Path(info.filename).name.lower(): info for info in archive.infolist() if not info.is_dir()}
        results, problems = [], []
        for image in openq4_images(runtime):
            record = codeview_record(image)
            relative = image.relative_to(runtime).as_posix()
            if record is None:
                problems.append(f"{relative} has no CodeView record")
                continue
            guid, age, name = record
            pdb = Path(name.replace("\\", "/")).name.lower()
            info = entries.get(pdb)
            if info is None:
                problems.append(f"{relative}: {pdb} is not in the symbols archive")
                results.append({"image": relative, "pdb": pdb, "present": False})
                continue
            content = archive.read(info)
            matches = guid in content
            if not matches:
                problems.append(f"{relative}: {pdb} does not carry the image's GUID")
            results.append({"image": relative, "pdb": pdb, "present": True, "guidMatches": matches, "age": age})
    return {"symbols": str(symbols), "images": results, "problems": problems, "passed": not problems}


def command_check_symbols(args: argparse.Namespace) -> int:
    report = check_symbols(args.runtime.resolve(), args.symbols.resolve())
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / "symbols.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


def command_check_signing(args: argparse.Namespace) -> int:
    runtime = args.runtime.resolve()
    statuses: dict[str, str] = {}
    if sys.platform == "win32":
        images = [str(path) for path in sorted(runtime.rglob("*"))
                  if path.is_file() and path.suffix.lower() in (".exe", ".dll")]
        script = ("$files = $env:OPENQ4_REL_FILES | ConvertFrom-Json; foreach ($f in $files) { "
                  "'{0}|{1}' -f $f, (Get-AuthenticodeSignature -LiteralPath $f).Status }")
        output = subprocess.run(["powershell", "-NoProfile", "-Command", script], capture_output=True, text=True,
                                timeout=600, env=dict(os.environ, OPENQ4_REL_FILES=json.dumps(images))).stdout
        for line in output.splitlines():
            if "|" in line:
                name, status = line.rsplit("|", 1)
                statuses[Path(name).relative_to(runtime).as_posix()] = status.strip()
        missing = sorted(Path(image).relative_to(runtime).as_posix() for image in images
                         if Path(image).relative_to(runtime).as_posix() not in statuses)
        for name in missing:
            statuses[name] = "Unchecked"
    broken = sorted(name for name, status in statuses.items() if status not in ("NotSigned", "Valid"))
    report = {"runtime": str(runtime), "statuses": statuses, "unsigned": sorted(n for n, s in statuses.items()
                                                                                 if s == "NotSigned"),
              "invalid": broken, "passed": not broken}
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / "signing.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


def command_check_layout(args: argparse.Namespace) -> int:
    runtime = args.runtime.resolve()
    problems = check_layout(runtime)
    report = {"runtime": str(runtime), "passed": not problems, "problems": problems,
              "engine": version_line(runtime / client_name())}
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / "layout.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if not problems else 1


def history(results: list[dict[str, Any]], case_id: str) -> str:
    statuses = [r["status"] for r in results if r["case"] == case_id]
    return " → ".join(statuses) if statuses else "—"


def write_report(output: Path) -> Path:
    candidate = load_candidate(output)
    results = read_results(output)
    by_case = latest(results)
    engine = candidate.get("engine") or {}
    lines = [
        f"# Release qualification: {candidate['label']}",
        "",
        f"- Frozen: {candidate['frozen']} on {candidate['platform']}",
        f"- Engine: `{engine.get('version')}`; source commit `{candidate.get('sourceCommit')}`",
        f"- Runtime: `{candidate['runtime']}`, manifest SHA-256 `{candidate['manifestSha256']}` "
        f"({len(candidate['files'])} files)",
    ]
    if candidate.get("package"):
        lines.append(f"- Package: `{candidate['package']['path']}`, SHA-256 `{candidate['package']['sha256']}`")
    retail = candidate.get("retail") or {}
    if retail:
        lines.append(f"- Retail assets: `{retail.get('basepath')}`, {len(retail.get('packs', {}))} pk4s, "
                     f"{retail.get('looseFiles', 0)} loose files in q4base")
    previous = candidate.get("previousRelease") or {}
    if previous:
        lines.append(f"- Previous release: `{(previous.get('engine') or {}).get('version')}` at `{previous.get('runtime')}`")
    gpus = (candidate.get("host") or {}).get("gpus") or []
    if gpus:
        lines.append("- GPUs: " + "; ".join(f"{g.get('Name')} ({g.get('DriverVersion')})" for g in gpus))
    counts: dict[str, int] = {}
    for case in MATRIX:
        status = by_case.get(case.id, {}).get("status", "pending" if case.kind == "automated" else "awaiting review")
        counts[status] = counts.get(status, 0) + 1
    lines += ["", "**Latest results:** " + ", ".join(f"{count} {status}" for status, count in sorted(counts.items())), ""]
    for task in ("REL-01", "REL-02", "REL-03", "REL-04"):
        lines += [f"## {task}", "", "| Case | Row | Latest | Attempts | Detail |", "| --- | --- | --- | --- | --- |"]
        for case in (c for c in MATRIX if c.task == task):
            result = by_case.get(case.id)
            status = result["status"] if result else ("pending" if case.kind == "automated" else "awaiting review")
            detail = (result or {}).get("reason") or ""
            if result and result.get("seconds"):
                detail = (detail + f" ({result['seconds']} s)").strip()
            lines.append(f"| `{case.id}` | {case.row} | {status} | {history(results, case.id)} | {detail} |")
        lines.append("")
    human = [case for case in MATRIX if case.kind == "human"]
    if human:
        lines += ["## Human and physical reviews", ""]
        for case in human:
            lines.append(f"- **{case.row}** (`{case.id}`): {case.procedure}")
        lines.append("")
    path = evidence_paths(output)[2]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def command_report(args: argparse.Namespace) -> int:
    print(write_report(args.output.resolve()))
    return 0


def command_list(args: argparse.Namespace) -> int:
    for case in MATRIX:
        needs = f" needs={','.join(case.needs)}" if case.needs else ""
        print(f"{case.id:38} {case.task} {case.kind:9}{needs}  {case.row}")
    return 0


# ---------------------------------------------------------------------------
# Self-test


def self_test() -> int:
    ids = [case.id for case in MATRIX]
    assert len(ids) == len(set(ids)), "duplicate case ids"
    for case in MATRIX:
        assert case.task in ("REL-01", "REL-02", "REL-03", "REL-04"), case.id
        assert case.id.startswith(case.task.lower().replace("-", "") + "."), case.id
        if case.kind == "automated":
            assert case.command and case.harness and case.summary, case.id
            assert (ROOT / case.harness).is_file(), f"{case.id}: missing harness {case.harness}"
            script = next((part for part in case.command if part.endswith(".py")), "")
            assert script.replace("{root}/", "") == case.harness, f"{case.id}: command runs {script}"
        else:
            assert case.kind == "human" and case.procedure and not case.command, case.id
    rows = {case.row for case in MATRIX}
    for required in ("Dedicated server role", "MP client against a dedicated server", "SP save and load",
                     "Clean install", "Upgrade over the previous release's profile", "Network loss and reconnect",
                     "Renderer recovery", "Audible sound review", "VR comfort review"):
        assert required in rows, required

    scratch = ROOT / ".tmp"
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="release-qualification-", dir=scratch) as temp:
        root = Path(temp)
        runtime = root / "runtime"
        (runtime / "baseoq4").mkdir(parents=True)
        (runtime / "crashes").mkdir()
        client = runtime / client_name()
        client.write_bytes(b"MZ\0\0openQ4 0.16.0+g0123456789abcdef0123456789abcdef01234567\0pad")
        (runtime / ded_name()).write_bytes(b"openQ4 0.16.0+g0123456789abcdef0123456789abcdef01234567\0")
        # Staged Linux and macOS binaries carry no suffix; check_runtime knows
        # them by their execute bit, as staging sets it. Windows ignores this.
        for binary in (client, runtime / ded_name()):
            binary.chmod(binary.stat().st_mode | 0o111)
        (runtime / "baseoq4" / "pak0.pk4").write_bytes(b"pack")
        (runtime / "crashes" / "old.log").write_text("ignored")
        stamp = version_line(client)
        assert stamp == {"version": "0.16.0+g0123456789abcdef0123456789abcdef01234567",
                         "commit": "0123456789abcdef0123456789abcdef01234567", "dirty": False}, stamp
        dirty = root / "dirty.exe"
        dirty.write_bytes(b"openQ4 0.15.0-dev+gd9fe3dd5.dirty\0openQ4 0.15.0-dev\0")
        assert version_line(dirty) == {"version": "0.15.0-dev+gd9fe3dd5.dirty", "commit": "d9fe3dd5", "dirty": True}
        stable = root / "stable.exe"
        stable.write_bytes(b"\0openQ4 0.16.0\0openQ4 %s\0")
        assert version_line(stable) == {"version": "0.16.0", "commit": None, "dirty": False}, version_line(stable)
        manifest = hash_manifest(runtime)
        assert set(manifest) == {client_name(), ded_name(), "baseoq4/pak0.pk4"}, sorted(manifest)
        assert check_runtime(runtime, manifest, deep=True) == []
        # A rebuilt binary with an unchanged size and timestamp is still caught.
        stat = client.stat()
        client.write_bytes(client.read_bytes().replace(b"pad", b"PAD"))
        os.utime(client, ns=(stat.st_atime_ns, stat.st_mtime_ns))
        assert check_runtime(runtime, manifest, deep=False) == [f"content changed {client_name()}"]
        (runtime / "extra.dll").write_bytes(b"x")
        (runtime / "baseoq4" / "pak0.pk4").unlink()
        problems = check_runtime(runtime, manifest, deep=False)
        assert "added extra.dll" in problems and "removed baseoq4/pak0.pk4" in problems, problems
        assert crash_reports(runtime) == ["old.log"]

        # Each image's PDB must be in the symbols archive and carry its GUID.
        guid = bytes(range(16))
        image_dir = root / "symbol-runtime"
        image_dir.mkdir()
        (image_dir / "openQ4-client_x64.exe").write_bytes(b"MZ" + b"RSDS" + guid + (3).to_bytes(4, "little") +
                                                         b"E:\\build\\openQ4-client_x64.pdb\0")
        (image_dir / "renderer-vk_x64.dll").write_bytes(b"MZ" + b"RSDS" + bytes(16) + (1).to_bytes(4, "little") +
                                                       b"renderer-vk_x64.pdb\0")
        (image_dir / "OpenAL32.dll").write_bytes(b"MZ no record")
        symbols_zip = root / "openq4-0.16.0-windows-x64-symbols.zip"
        with zipfile.ZipFile(symbols_zip, "w") as archive:
            archive.writestr("openq4-0.16.0-windows-x64-symbols/openQ4-client_x64.pdb", b"MSF" + guid)
        result = check_symbols(image_dir, symbols_zip)
        assert [r["image"] for r in result["images"]] == ["openQ4-client_x64.exe", "renderer-vk_x64.dll"], result
        assert result["images"][0]["guidMatches"] and result["problems"] == [
            "renderer-vk_x64.dll: renderer-vk_x64.pdb is not in the symbols archive"], result
        with zipfile.ZipFile(symbols_zip, "w") as archive:
            archive.writestr("s/openQ4-client_x64.pdb", b"MSF other")
            archive.writestr("s/renderer-vk_x64.pdb", b"MSF" + bytes(16))
        result = check_symbols(image_dir, symbols_zip)
        assert result["problems"] == ["openQ4-client_x64.exe: openq4-client_x64.pdb does not carry the image's GUID"], \
            result

        # A hard-linked view holds the same files and goes away without touching them.
        STOCK_RUNTIME_PARENT.mkdir(parents=True, exist_ok=True)
        view = STOCK_RUNTIME_PARENT / "rel-selftest-view"
        make_link_view(runtime, view)
        assert sorted(n for n, _ in runtime_files(view)) == sorted(n for n, _ in runtime_files(runtime))
        assert os.path.samefile(view / client_name(), runtime / client_name())
        remove_link_view(view)
        assert not view.exists() and (runtime / client_name()).is_file()
        try:
            make_link_view(runtime, root / "elsewhere")
        except SystemExit:
            pass
        else:
            raise AssertionError("a view outside .tmp/stock-runtime was made")

        # A runtime extracted from an archive matches it entry for entry.
        archive = root / "package.zip"
        with zipfile.ZipFile(archive, "w") as package:
            package.writestr("openq4-0.16.0-windows-x64/" + client_name(), client.read_bytes())
            package.writestr("openq4-0.16.0-windows-x64/" + ded_name(), (runtime / ded_name()).read_bytes())
            package.writestr("openq4-0.16.0-windows-x64/baseoq4/pak0.pk4", b"pack")
        comparison = compare_with_archive(runtime, archive)
        assert comparison["prefix"] == "openq4-0.16.0-windows-x64" and comparison["missing"] == ["baseoq4/pak0.pk4"],             comparison
        assert comparison["notInPackage"] == ["extra.dll"] and not comparison["matches"], comparison
        (runtime / "baseoq4" / "pak0.pk4").write_bytes(b"pakc")
        assert compare_with_archive(runtime, archive)["mismatched"] == ["baseoq4/pak0.pk4"]
        (runtime / "baseoq4" / "pak0.pk4").write_bytes(b"pack")
        assert compare_with_archive(runtime, archive)["matches"]

        # Freeze, an unmet need, a pass, a failure and a changed runtime.
        runtime2 = root / "runtime2"
        (runtime2 / "baseoq4").mkdir(parents=True)
        (runtime2 / client_name()).write_bytes(b"openQ4 0.16.0+gabcdef1\0")
        output = root / "evidence"
        assert resource_version(client) is None
        resourced = root / "resourced.exe"
        resourced.write_bytes(b"x" * 7 + struct.pack("<4I", 0xFEEF04BD, 0x10000, 16, 2700) + b"y")
        assert resource_version(resourced) == (0, 16, 0, 2700)
        commit, binding = bind_source_commit({"version": "0.16.0", "commit": None, "dirty": False}, resourced,
                                             root, None)
        assert commit is None and "--source-commit" in binding["error"], binding
        head, count = git(ROOT, "rev-parse", "HEAD"), git(ROOT, "rev-list", "--count", "HEAD")
        if head and count:
            stable = {"version": "0.16.0", "commit": None, "dirty": False}
            resourced.write_bytes(struct.pack("<4I", 0xFEEF04BD, 0x10000, 16, int(count)))
            assert bind_source_commit(stable, resourced, ROOT, head)[0] == head
            resourced.write_bytes(struct.pack("<4I", 0xFEEF04BD, 0x10000, 16, int(count) + 1))
            commit, binding = bind_source_commit(stable, resourced, ROOT, head)
            assert commit is None and "history length" in binding["error"], binding
        args = argparse.Namespace(runtime=runtime2, output=output, force=False, source_root=root, label="test",
                                  source_commit=None,
                                  basepath=None, awakening=None, previous_release=None, package=None, note=None,
                                  host=False)
        assert command_freeze(args) == 1  # g-sha does not resolve in a non-repository
        candidate = load_candidate(output)
        assert candidate["engine"]["commit"] == "abcdef1" and candidate["sourceCommit"] is None
        run_args = argparse.Namespace(min_free_gb=0.0, note="self-test")
        unmet = run_case(MATRIX[2], candidate, output, run_args)
        assert unmet["status"] == "not-run" and "basepath" in unmet["reason"], unmet
        passing = Case("rel01.selftest.pass", "REL-01", "Self-test", "s", (sys.executable, "-c", "print('ok')"),
                       timeout=60)
        result = run_case(passing, candidate, output, run_args)
        assert result["status"] == "pass" and result["exitCode"] == 0, result
        append_result(output, result)
        failing = Case("rel01.selftest.pass", "REL-01", "Self-test", "s", (sys.executable, "-c", "raise SystemExit(3)"),
                       timeout=60)
        result = run_case(failing, candidate, output, run_args)
        assert result["status"] == "fail" and result["attempt"] == 2 and result["exitCode"] == 3, result
        append_result(output, result)
        assert history(read_results(output), "rel01.selftest.pass") == "pass → fail"
        swap = Case("rel01.selftest.swap", "REL-01", "Self-test", "s",
                    (sys.executable, "-c", f"open(r'{runtime2 / client_name()}','ab').write(b'x')"), timeout=60)
        result = run_case(swap, candidate, output, run_args)
        assert result["status"] == "invalid" and result["runtimeProblemsAfter"], result
        blocked = run_case(passing, candidate, output, run_args)
        assert blocked["status"] == "blocked", blocked
        report = write_report(output).read_text(encoding="utf-8")
        assert "| `rel01.selftest.pass`" not in report  # only matrix cases are reported
        assert "## REL-02" in report and "VR comfort review" in report
    print("release_qualification: self-test ok")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    freeze = commands.add_parser("freeze", help="hash a candidate runtime and record its identity")
    freeze.add_argument("--runtime", type=Path, required=True)
    freeze.add_argument("--output", type=Path, required=True)
    freeze.add_argument("--source-root", type=Path, default=ROOT)
    freeze.add_argument("--source-commit", help="full SHA of a stable build, whose version line names no commit")
    freeze.add_argument("--basepath", type=Path, help="retail Quake 4 root holding q4base (a packs-only view on Windows)")
    freeze.add_argument("--awakening", type=Path, help="root holding the q4xbase Awakening content")
    freeze.add_argument("--previous-release", type=Path, help="runtime of the previous release, for upgrade cases")
    freeze.add_argument("--package", type=Path, help="the release archive the runtime was extracted from")
    freeze.add_argument("--label")
    freeze.add_argument("--note", action="append")
    freeze.add_argument("--force", action="store_true")
    run = commands.add_parser("run", help="run matrix cases against the frozen candidate")
    run.add_argument("--output", type=Path, required=True)
    run.add_argument("--case", nargs="+")
    run.add_argument("--task", nargs="+", choices=("REL-01", "REL-02", "REL-03", "REL-04"))
    run.add_argument("--rerun-failed", action="store_true", help="only cases whose latest attempt did not pass")
    run.add_argument("--skip-passed", action="store_true")
    run.add_argument("--min-free-gb", type=float, default=8.0, help="free commit to wait for before each case")
    run.add_argument("--note", help="recorded with every attempt of this invocation")
    human = commands.add_parser("record-human", help="record a reviewer's verdict for a human case")
    human.add_argument("--output", type=Path, required=True)
    human.add_argument("--case", required=True)
    human.add_argument("--verdict", choices=("pass", "fail", "partial"), required=True)
    human.add_argument("--reviewer", required=True)
    human.add_argument("--notes", required=True)
    human.add_argument("--device", action="append")
    report = commands.add_parser("report", help="write summary.md")
    report.add_argument("--output", type=Path, required=True)
    symbols = commands.add_parser("check-symbols", help="check the symbols archive against the runtime's images")
    symbols.add_argument("--runtime", type=Path, required=True)
    symbols.add_argument("--symbols", type=Path, required=True)
    symbols.add_argument("--output", type=Path)
    signing = commands.add_parser("check-signing", help="record each image's code signature status")
    signing.add_argument("--runtime", type=Path, required=True)
    signing.add_argument("--output", type=Path)
    layout = commands.add_parser("check-layout", help="check a runtime's package layout")
    layout.add_argument("--runtime", type=Path, required=True)
    layout.add_argument("--output", type=Path)
    commands.add_parser("list", help="list the matrix")
    commands.add_parser("self-test", help="check the runner on synthetic runtimes")
    args = parser.parse_args(argv)
    handlers = {"freeze": command_freeze, "run": command_run, "record-human": command_record_human,
                "report": command_report, "check-layout": command_check_layout, "list": command_list,
                "check-symbols": command_check_symbols, "check-signing": command_check_signing,
                "self-test": lambda _: self_test()}
    return handlers[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
