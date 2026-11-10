#!/usr/bin/env python3
"""openQ4 performance suite: repeatable frame-time benchmarks on stock Quake 4 maps.

The suite launches a staged openQ4 runtime (a release package or `.install`), plays
scripted scenarios on the retail single-player maps and records the time of every
frame. Nothing is driven by keyboard or mouse: every scenario is a console script
executed through `g_autoExecAfterMapLoad`, so a run plays back identically on any
build that has the same commands (0.13.2 and later). Each launch is one scenario;
pass 1 of a launch is the cold pass (first use of shaders, models, sounds and GUIs
after the map loaded) and later passes are warm.

Per pass it records
  - the engine's own frame-pacing snapshot (`framePacingReset`/`framePacingSnapshot`),
  - a per-frame trace from `profileFrameWork <n> <csv>` (builds that have it),
  - the system load during the window (CPU %, GPU utilisation/clock/temperature),
and per launch the level-load time, the effective settings and the renderer identity.

    python tools/tests/performance_suite.py list
    python tools/tests/performance_suite.py run --output .tmp/perf-suite/<name> \
        --config head=.tmp/stock-runtime/perf-head:gl:own-ultra \
        --config v0132=.tmp/stock-runtime/perf-0132:gl:own-ultra \
        --scenarios core --rounds 3 --passes 2
    python tools/tests/performance_suite.py report --runs .tmp/perf-suite/<name> --out .tmp/perf-suite/<name>/report

Budgets (FPS-01): the reference runs measure each tier on both backends (`own-ultra`, `own-quality`,
`own-enhanced`), `budgets` declares the warm mean and p99 per scenario, backend and tier with the gate
limits, and `check` gates a candidate run against that file (exit 1 on a regression):

    python tools/tests/performance_suite.py budgets --runs <gl run> <vk run> --machine "<name>" \
        --out docs/dev/performance-budgets.json --markdown <dir>/budgets.md
    python tools/tests/performance_suite.py check --budgets docs/dev/performance-budgets.json --runs <candidate runs>

Beyond the plain matrix: `--toggle LABEL=cmd;cmd` turns a launch into a feature ladder
(base0, toggle, base1, toggle ...; the report prices every toggle against its neighbouring
base windows), `--attribution` adds renderer metrics, GPU timers and the benchmark capture
to each pass, `--cpu-sample N` records N seconds of main-thread stacks with the runtime's
own directories as the symbol path, and settings profiles pin a build to its own preset or
to another release's cvars (`eq-0132-ultra`). A launch whose GPU stays idle while frames are
logged did not hold the foreground fullscreen window and rendered nothing; the run waits
before every launch, repeats such a launch (`--max-retries`) and the report flags or drops
(`--drop-suspect`) any that remain.

Runs are visible fullscreen windows at the requested size; use --windowed/--hidden only
for smoke tests of the scripts (hidden windows are capped at 60 Hz by design).
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import json
import math
import os
import re
import shutil
import statistics
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BASEPATH_CANDIDATES = (
    Path(r"E:\SteamLibrary\steamapps\common\Quake 4"),
    Path(r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"),
)
CLIENT_EXE = "openQ4-client_x64.exe"
CONFIG_FILE = "openQ4Config.cfg"
GAME_DIR = "baseoq4"


# ----------------------------------------------------------------------------
# Scenarios
# ----------------------------------------------------------------------------

@dataclass(frozen=True)
class Scenario:
    id: str
    map: str
    category: str                      # core | additional | hitch | transition
    purpose: str
    area: str
    setup: tuple[str, ...] = ()        # once per launch, after the map is live
    pass_setup: tuple[str, ...] = ()   # before every pass
    motion: tuple[str, ...] = ()       # commands during the measured window (waits are padded to the duration)
    settle_ms: int = 2500              # wait between pass setup and the measured window
    teardown: tuple[str, ...] = ()     # after every pass
    passes: int | None = None          # override the run-wide pass count
    events: tuple[tuple[str, tuple[str, ...]], ...] = ()   # hitch scenarios: named first-use events
    transition_map: str = ""           # transition scenarios: map loaded after the pass
    head_only_events: tuple[str, ...] = ()


def _ring(center: tuple[float, float, float], radius: float, count: int, start_deg: float = 0.0) -> list[tuple[float, float, float]]:
    points = []
    for i in range(count):
        a = math.radians(start_deg + 360.0 * i / count)
        points.append((center[0] + radius * math.cos(a), center[1] + radius * math.sin(a), center[2]))
    return points


def _spawn(classname: str, name: str, origin: tuple[float, float, float], angle: float = 0.0) -> str:
    return f'spawn {classname} name {name} origin "{origin[0]:.0f} {origin[1]:.0f} {origin[2]:.0f}" angle {angle:.0f}'


AD1_SPAWN = (10324.0, -6964.0, -36.0)
# Open battlefield west of the landing zone, below the bunker wall where the map's own marines
# and strogg fight (ground is at z 0..16; spawns start slightly above it).
AD1_ARENA = (9400.0, -6700.0, 24.0)
AD1_ARENA_VIEW = "setviewpos 9400 -6700 72 0 90 0"


def _combat_wave(tag: str) -> tuple[str, ...]:
    """A mixed strogg/marine wave around the Air Defense 1 landing zone."""
    lines: list[str] = []
    strogg = ["monster_strogg_marine", "monster_strogg_marine_mgun", "monster_strogg_marine_sgun", "monster_grunt",
              "monster_strogg_marine", "monster_strogg_marine_mgun", "monster_gunner", "monster_strogg_marine_sgun"]
    marines = ["char_marine", "char_marine_hyperblaster", "char_marine", "char_marine_shotgun", "char_marine", "char_marine_hyperblaster"]
    for i, (cls, pos) in enumerate(zip(strogg, _ring(AD1_ARENA, 520.0, len(strogg), 10.0))):
        lines.append(_spawn(cls, f"{tag}_s{i}", pos, 180.0 + 360.0 * i / len(strogg)))
    for i, (cls, pos) in enumerate(zip(marines, _ring(AD1_ARENA, 300.0, len(marines), 40.0))):
        lines.append(_spawn(cls, f"{tag}_m{i}", pos, 360.0 * i / len(marines)))
    lines.append(_spawn("monster_lt_tank", f"{tag}_t0", (AD1_ARENA[0] + 760.0, AD1_ARENA[1] - 120.0, AD1_ARENA[2]), 180.0))
    lines.append(_spawn("monster_gladiator", f"{tag}_g0", (AD1_ARENA[0] - 700.0, AD1_ARENA[1] + 260.0, AD1_ARENA[2]), 0.0))
    return tuple(lines)


def _effects_wave(tag: str) -> tuple[str, ...]:
    lines: list[str] = []
    for i, pos in enumerate(_ring(AD1_ARENA, 360.0, 12, 15.0)):
        lines.append(_spawn("moveable_barrel_exploding1", f"{tag}_b{i}", pos))
    for i, pos in enumerate(_ring(AD1_ARENA, 620.0, 4, 45.0)):
        lines.append(_spawn("monster_gunner", f"{tag}_gn{i}", pos, 180.0 + 90.0 * i))
    for i, pos in enumerate(_ring(AD1_ARENA, 640.0, 2, 0.0)):
        lines.append(_spawn("monster_lt_tank", f"{tag}_lt{i}", pos, 180.0 * i))
    for i, pos in enumerate(_ring(AD1_ARENA, 260.0, 4, 0.0)):
        lines.append(_spawn("char_marine_hyperblaster", f"{tag}_hm{i}", pos, 90.0 * i))
    return tuple(lines)


def _effects_motion(tag: str) -> tuple[str, ...]:
    """Sweep while blowing a barrel every 2.5 s (explosion light, debris, smoke)."""
    lines = ["benchmarkViewSweep 720 {duration}"]
    for i in range(12):
        lines += ["waitMsec 2400", f"damage {tag}_b{i} 1000"]
    return tuple(lines)


def _remove_names(spawn_lines: tuple[str, ...]) -> tuple[str, ...]:
    """`remove <name>` for every entity a spawn list creates (console remove acts immediately)."""
    names = [m.group(1) for line in spawn_lines for m in [re.search(r"name (\S+)", line)] if m]
    return tuple(f"remove {name}" for name in names)


SCENARIOS: dict[str, Scenario] = {}


def _register(s: Scenario) -> None:
    SCENARIOS[s.id] = s


_register(Scenario(
    id="outdoor_airdefense1", map="game/airdefense1", category="core",
    purpose="outdoor portal sky with long views across the Air Defense valley, scripted opening battle, BSE smoke and fires",
    area="raised vantage above the landing zone (spawn +456 units), two full turns",
    setup=("god", "notarget", "noclip"),
    pass_setup=("setviewpos 10324 -6964 420 12 0 0",),
    motion=("benchmarkViewSweep 720 {duration}",),
))
_register(Scenario(
    id="outdoor_convoy1", map="game/convoy1", category="core",
    purpose="outdoor portal sky over the convoy canyon: the longest sight lines in the campaign, terrain, vehicles and distant refinery",
    area="canyon vantage above the start of the convoy road (spawn +460 units), two full turns",
    setup=("god", "notarget", "noclip"),
    pass_setup=("setviewpos -20115 56796 700 12 0 0",),
    motion=("benchmarkViewSweep 720 {duration}",),
))
_register(Scenario(
    id="combat_airdefense1", map="game/airdefense1", category="core",
    purpose="many AI fighting at once: a spawned wave of 8 strogg, 6 marines, a light tank and a gladiator around the player, with weapon fire, projectiles, ragdolls and damage effects",
    area="Air Defense 1 battlefield north-west of the landing zone at eye height, two full turns while the wave fights",
    setup=("god", "notarget", "noclip"),
    pass_setup=("killMonsters", "killRagdolls", "killMoveables", "wait 10", AD1_ARENA_VIEW) + _combat_wave("pw{pass}"),
    motion=("benchmarkViewSweep 720 {duration}",),
    teardown=_remove_names(_combat_wave("pw{pass}")),
    settle_ms=1500,
))
_register(Scenario(
    id="effects_airdefense1", map="game/airdefense1", category="core",
    purpose="effect-heavy: twelve exploding barrels detonated during the window, gunner grenades, light-tank rockets and hyperblaster fire, dynamic lights and shadows, debris, smoke, plus the preset's post-processing",
    area="Air Defense 1 battlefield at eye height, two full turns with an explosion every 2.4 s",
    setup=("god", "notarget", "noclip"),
    pass_setup=("killMonsters", "killRagdolls", "killMoveables", "wait 10", AD1_ARENA_VIEW) + _effects_wave("pe{pass}"),
    motion=_effects_motion("pe{pass}"),
    teardown=_remove_names(_effects_wave("pe{pass}")),
    settle_ms=1500,
))
_register(Scenario(
    id="indoor_storage2", map="game/storage2", category="additional",
    purpose="tight indoor Strogg deck with dense local lights and stencil/shadow-map shadows; the map where the equal-settings regression was first measured",
    area="Storage 2 upper deck by the tactical squad spawn (-656 1264), two full turns",
    setup=("god", "notarget", "noclip"),
    pass_setup=("setviewpos -656 1264 -2688 0 0 0",),
    motion=("benchmarkViewSweep 720 {duration}",),
))
_register(Scenario(
    id="indoor_hub2", map="game/hub2", category="additional",
    purpose="large multi-level indoor hall with the densest shadow-casting light cluster of the campaign (55 shadowing lights within 700 units)",
    area="Nexus Hub 2 processing hall at the sentry spawn (3568 -5780), two full turns",
    setup=("god", "notarget", "noclip"),
    pass_setup=("setviewpos 3568 -5780 -296 0 0 0",),
    motion=("benchmarkViewSweep 720 {duration}",),
))
_register(Scenario(
    id="vehicle_walker", map="game/walker", category="additional",
    purpose="vehicle section: the player pilots the walker mech through its scripted outdoor assault (vehicle physics, cockpit, weapons, many enemies)",
    area="Walker level from the mech bay, two full turns from inside the vehicle",
    setup=("god", "notarget", "wait 30", 'script "$player1.enterVehicle( $playerWalker );"', "wait 60"),
    motion=("benchmarkViewSweep 720 {duration}",),
    passes=1,
))
_register(Scenario(
    id="transition_ad1_ad2", map="game/airdefense1", category="transition",
    purpose="level transition cost: load Air Defense 2 from inside Air Defense 1 and time the map change",
    area="map change airdefense1 -> airdefense2",
    setup=("god", "notarget"),
    motion=("waitMsec 3000",),
    settle_ms=500,
    passes=1,
    transition_map="game/airdefense2",
))
_register(Scenario(
    id="hitch_firstuse", map="game/airdefense1", category="hitch",
    purpose="first-use hitches: each event runs once after the map is live while every frame is recorded; the frame that absorbs the first use shows as the spike",
    area="Air Defense 1 battlefield, static view",
    setup=("god", "notarget", "noclip", AD1_ARENA_VIEW),
    events=(
        ("give_all", ("give all",)),
        ("spawn_gladiator", (_spawn("monster_gladiator", "hg0", (AD1_ARENA[0], AD1_ARENA[1] + 400.0, AD1_ARENA[2])),)),
        ("spawn_tank", (_spawn("monster_lt_tank", "ht0", (AD1_ARENA[0] + 300.0, AD1_ARENA[1] + 500.0, AD1_ARENA[2])),)),
        ("explosion", (_spawn("moveable_barrel_exploding1", "hb0", (AD1_ARENA[0] - 100.0, AD1_ARENA[1] + 250.0, AD1_ARENA[2])), "waitMsec 400", "damage hb0 1000")),
        ("first_shot", ("in_presentationViewProbeAttack 1", "waitMsec 250", "in_presentationViewProbeAttack 0")),
        ("scope_zoom", ("give weapon_railgun", "waitMsec 400", "testWeaponScope 1", "waitMsec 1200", "testWeaponScope 0")),
        ("flashlight", ("testFlashlight",)),
    ),
    head_only_events=("first_shot", "scope_zoom", "flashlight"),
    passes=1,
))

CORE_SET = ("outdoor_airdefense1", "outdoor_convoy1", "combat_airdefense1", "effects_airdefense1")
ALL_TIMED = CORE_SET + ("indoor_storage2", "indoor_hub2", "vehicle_walker")


# ----------------------------------------------------------------------------
# Settings profiles
# ----------------------------------------------------------------------------

@dataclass(frozen=True)
class Settings:
    name: str
    preset: str | None              # applyPerformancePreset argument, None = the build's first-launch choice
    overrides: tuple[tuple[str, str], ...] = ()
    description: str = ""


# Values of the 0.13.2 Ultra preset for targets the current Ultra sets differently, so the
# current build can be measured at the settings the 0.13.2 Ultra preset produced.
EQ_0132_ULTRA = (
    ("r_multiSamples", "8"), ("r_postAA", "1"), ("r_temporalAA", "0"),
    ("r_useShadowMap", "0"), ("r_shadowMapSize", "2048"), ("r_shadowMapPointSize", "512"), ("r_shadowMapAtlasSize", "4096"),
    ("r_shadowMapCSM", "0"), ("r_shadowMapFilterMode", "0"), ("r_shadowMapFilterRadius", "0.75"), ("r_shadowMapFilterTaps", "9"),
    ("r_shadowMapPointFilterMode", "0"), ("r_shadowMapPointFilterTaps", "9"),
    ("r_bloom", "0"), ("r_ssao", "0"), ("r_ssaoSamples", "20"), ("r_hdrToneMap", "0"),
    ("r_motionBlur", "0"), ("r_motionBlurSamples", "8"), ("r_pbrMaterials", "0"), ("r_portalsDistanceCull", "1"),
    ("s_numberOfSpeakers", "6"), ("s_quadraticFalloff", "1"), ("s_radioChatterFraction", "0.5"),
)

SETTINGS: dict[str, Settings] = {
    "own-ultra": Settings("own-ultra", "ultra", (), "the build's own Ultra preset, frame cap and vsync off"),
    "own-quality": Settings("own-quality", "quality", (), "the build's own Quality preset, frame cap and vsync off"),
    "own-enhanced": Settings("own-enhanced", "enhanced", (), "the build's own Enhanced preset, frame cap and vsync off"),
    "own-default": Settings("own-default", None, (), "whatever the build applies on a first launch (auto-detected preset), frame cap and vsync off"),
    "eq-0132-ultra": Settings("eq-0132-ultra", "ultra", EQ_0132_ULTRA, "Ultra preset, then every cvar whose effective value differs from the 0.13.2 Ultra configuration (listCvars diff, 24 values) forced to the 0.13.2 value"),
    "own-ultra-msaa4": Settings("own-ultra-msaa4", "ultra", (("r_multiSamples", "4"),), "Ultra preset with 4x MSAA (MSAA needs a restart, so it cannot be an in-launch toggle)"),
    "own-ultra-msaa0": Settings("own-ultra-msaa0", "ultra", (("r_multiSamples", "0"),), "Ultra preset without MSAA"),
    "own-ultra-nopost": Settings("own-ultra-nopost", "ultra", (("r_ssao", "0"), ("r_bloom", "0"), ("r_hdrToneMap", "0"), ("r_motionBlur", "0"), ("r_postAA", "1")), "Ultra preset with the screen-space post chain off (SMAA medium kept), shadow maps and the preset's MSAA kept"),
    "own-ultra-msaa8": Settings("own-ultra-msaa8", "ultra", (("r_multiSamples", "8"),), "Ultra preset with 8x MSAA, the 0.13.2 Ultra sample count (a GPU with GL_MAX_SAMPLES 32 renders the preset's 16)"),
}


@dataclass
class RunConfig:
    label: str
    runtime: Path
    api: str                 # gl | vk
    settings: Settings
    savepath: Path = field(default=Path())
    exe_sha256: str = ""
    has_frame_logger: bool = False
    has_head_cheats: bool = False
    cvars: dict[str, str] = field(default_factory=dict)


# ----------------------------------------------------------------------------
# Launching
# ----------------------------------------------------------------------------

def default_basepath() -> str:
    env = os.environ.get("OPENQ4_QUAKE4_BASEPATH")
    if env:
        return env
    for candidate in DEFAULT_BASEPATH_CANDIDATES:
        if (candidate / "q4base").is_dir():
            return str(candidate)
    return ""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def common_sets(cfg: RunConfig, log_name: str, width: int, height: int, fullscreen: bool, hidden: bool,
                basepath: str, extra: Iterable[tuple[str, str]] = ()) -> list[str]:
    sets: list[tuple[str, str]] = [
        ("win_allowMultipleInstances", "1"),
        ("logFile", "2"), ("logFileName", f"logs/{log_name}"), ("developer", "1"),
        ("com_timestampPrints", "2"),
        ("com_skipLoadingContinue", "1"), ("com_loadingContinueAutoAdvance", "1"),
        ("g_autoSkipCinematics", "1"), ("com_skipLogoVideos", "1"), ("g_autoScreenshot", "0"),
        ("com_showFPS", "0"), ("in_mouse", "0"), ("s_muteUnfocused", "0"),
        ("r_swapInterval", "0"), ("com_maxfps", "0"),
        ("r_renderApi", "vulkan" if cfg.api == "vk" else "gl"),
        ("r_fullscreen", "1" if fullscreen else "0"), ("r_mode", "-1"),
        ("r_customWidth", str(width)), ("r_customHeight", str(height)),
        ("r_windowWidth", str(width)), ("r_windowHeight", str(height)),
        ("r_fullscreenDesktop", "0"), ("r_borderless", "0"), ("r_borderlessDefaultMigrated", "1"),
        ("fs_savepath", str(cfg.savepath)), ("fs_devpath", str(cfg.savepath)), ("fs_game", GAME_DIR),
    ]
    if hidden:
        sets.append(("r_hiddenWindow", "1"))
    if basepath:
        sets.append(("fs_basepath", basepath))
    sets.extend(extra)
    args: list[str] = []
    for name, value in sets:
        args += ["+set", name, value]
    return args


LAUNCH_SETTLE_S = 3.0
SUSPECT_RETRY_SETTLE_S = 10.0


def launch(cfg: RunConfig, args: list[str], timeout_s: float, sampler: "LoadSampler | None" = None,
           on_start: "Any" = None) -> dict[str, Any]:
    """Starts the client and waits for it to exit. Every launch first waits LAUNCH_SETTLE_S: an exclusive-fullscreen
    client created while the desktop is still restoring from the previous one can miss the foreground, and a client
    that is not the foreground fullscreen window renders nothing (see launch_suspect)."""
    exe = cfg.runtime / CLIENT_EXE
    time.sleep(LAUNCH_SETTLE_S)
    started = time.time()
    proc = subprocess.Popen([str(exe), *args], cwd=str(cfg.runtime), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if sampler:
        sampler.start()
    if on_start:
        on_start(proc)
    timed_out = False
    try:
        proc.wait(timeout=timeout_s)
    except subprocess.TimeoutExpired:
        timed_out = True
        proc.kill()
        proc.wait(timeout=30)
    finally:
        if sampler:
            sampler.stop()
    return {"exit_code": proc.returncode, "timed_out": timed_out, "wall_s": round(time.time() - started, 1),
            "command": [str(exe), *args]}


class LoadSampler:
    """Samples system CPU and GPU load once a second while a launch runs.

    The GPU figures come from one streaming `nvidia-smi -lms 1000` process per launch: a separate
    query per sample took 2-5 s under a fullscreen client and timed out, losing every GPU figure."""

    GPU_QUERY = "--query-gpu=utilization.gpu,clocks.sm,temperature.gpu,power.draw,memory.used"

    def __init__(self) -> None:
        self.samples: list[dict[str, Any]] = []
        self.gpu_samples: list[dict[str, Any]] = []
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._gpu_thread: threading.Thread | None = None
        self._gpu_proc: subprocess.Popen[str] | None = None
        self._nvidia = shutil.which("nvidia-smi")
        try:
            import psutil  # noqa: F401
            self._psutil = True
        except ImportError:
            self._psutil = False

    def start(self) -> None:
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()
        if self._nvidia:
            try:
                self._gpu_proc = subprocess.Popen([self._nvidia, self.GPU_QUERY, "--format=csv,noheader,nounits", "-lms", "1000"],
                                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
                                                  creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            except OSError:
                self._gpu_proc = None
            if self._gpu_proc is not None:
                self._gpu_thread = threading.Thread(target=self._read_gpu, daemon=True)
                self._gpu_thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._gpu_proc is not None:
            self._gpu_proc.terminate()
            try:
                self._gpu_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self._gpu_proc.kill()
        if self._thread:
            self._thread.join(timeout=5)
        if self._gpu_thread:
            self._gpu_thread.join(timeout=5)

    def _run(self) -> None:
        while not self._stop.is_set():
            sample: dict[str, Any] = {"t": time.time()}
            if self._psutil:
                import psutil  # type: ignore
                sample["cpu"] = psutil.cpu_percent(interval=1.0)
            else:
                time.sleep(1.0)
            self.samples.append(sample)

    def _read_gpu(self) -> None:
        assert self._gpu_proc is not None and self._gpu_proc.stdout is not None
        for line in self._gpu_proc.stdout:
            parts = [p.strip() for p in line.split(",")]
            if len(parts) < 4:
                continue
            sample = {"t": time.time(), "gpu": _num(parts[0]), "gpu_mhz": _num(parts[1]), "gpu_c": _num(parts[2]), "gpu_w": _num(parts[3])}
            if len(parts) >= 5:
                sample["gpu_mem"] = _num(parts[4])
            self.gpu_samples.append(sample)

    def summary(self, start: float | None = None, end: float | None = None) -> dict[str, Any]:
        def within(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
            return [s for s in rows if (start is None or s["t"] >= start) and (end is None or s["t"] <= end)]
        cpu_rows, gpu_rows = within(self.samples), within(self.gpu_samples)
        result: dict[str, Any] = {"samples": len(cpu_rows), "gpu_samples": len(gpu_rows)}
        for rows, keys in ((cpu_rows, ("cpu",)), (gpu_rows, ("gpu", "gpu_mhz", "gpu_c", "gpu_w", "gpu_mem"))):
            for key in keys:
                values = [r[key] for r in rows if isinstance(r.get(key), (int, float))]
                if values:
                    result[key + "_mean"] = round(statistics.fmean(values), 1)
                    result[key + "_max"] = round(max(values), 1)
        return result


def _num(text: str) -> float | None:
    try:
        return float(text)
    except ValueError:
        return None


# ----------------------------------------------------------------------------
# Config launch: apply the preset once per config and archive it
# ----------------------------------------------------------------------------

def ensure_config(cfg: RunConfig, basepath: str, width: int, height: int, fullscreen: bool, hidden: bool, out_dir: Path, force: bool) -> dict[str, Any]:
    game_dir = cfg.savepath / GAME_DIR
    game_dir.mkdir(parents=True, exist_ok=True)
    marker = game_dir / "perf_suite_config.json"
    if marker.exists() and not force:
        return json.loads(marker.read_text(encoding="utf-8"))
    settings_cfg = game_dir / "perf_settings.cfg"
    settings_cfg.write_text("".join(f"set {name} {value}\n" for name, value in cfg.settings.overrides) or "// no overrides\n", encoding="ascii")
    args = common_sets(cfg, "perf_config.log", width, height, fullscreen, hidden, basepath)
    if cfg.settings.preset:
        args += ["+applyPerformancePreset", cfg.settings.preset]
    args += ["+exec", "perf_settings.cfg", "+listCvars", "+writeConfig", CONFIG_FILE, "+wait", "90", "+quit"]
    result = launch(cfg, args, timeout_s=240)
    log = game_dir / "logs" / "perf_config.log"
    text = log.read_text(encoding="latin-1", errors="replace") if log.exists() else ""
    cvars = parse_listcvars(text)
    info = {
        "label": cfg.label, "settings": cfg.settings.name, "preset": cfg.settings.preset,
        "overrides": list(cfg.settings.overrides), "launch": result,
        "version": text.splitlines()[0].strip() if text else "",
        "config_written": (game_dir / CONFIG_FILE).exists(),
        "cvars": cvars,
    }
    marker.write_text(json.dumps(info, indent=1), encoding="utf-8")
    if log.exists():
        shutil.copy2(log, out_dir / f"{cfg.label}_config.log")
    return info


KEY_CVARS = (
    "com_performancePreset", "com_machineSpec", "com_videoRam", "com_maxfps", "r_swapInterval", "r_renderApi", "r_multiSamples", "r_postAA",
    "r_temporalAA", "r_screenFraction", "image_anisotropy", "r_useShadowMap", "r_shadowMapSize", "r_shadowMapPointSize",
    "r_shadowMapFilterMode", "r_shadowMapFilterTaps", "r_shadowMapCSM", "r_shadowMapAtlasSize", "r_bloom", "r_bloomMipCount",
    "r_ssao", "r_ssaoSamples", "r_ssaoResolution", "r_hdrToneMap", "r_hdrSceneTarget", "r_motionBlur", "r_motionBlurSamples",
    "r_useLightGrid", "r_pbrMaterials", "r_portalsDistanceCull", "g_presentationInterpolation", "ui_retained", "ui_hudLayout",
    "r_vkRenderThread", "r_rendererModernVisible", "r_rendererForwardPlus", "s_useEAXReverb", "s_numberOfSpeakers",
    "r_fullscreen", "r_mode", "r_customWidth", "r_customHeight", "image_downSize", "image_usePrecompressedTextures",
    "com_preciseFrameWait", "s_asyncSampleDecode", "s_asyncStreamDecode", "image_prefetchLevelImages",
)

# listCvars prints `name<spaces>^7"value"`; com_timestampPrints adds a `[msec]` prefix.
LISTCVARS_RE = re.compile(r"^(?:\[\d+\])?\s*([A-Za-z_][A-Za-z0-9_]*)\s+(?:\^\d)?\"([^\"]*)\"\s*$", re.MULTILINE)


def parse_listcvars(text: str) -> dict[str, str]:
    cvars: dict[str, str] = {}
    for match in LISTCVARS_RE.finditer(text):
        cvars.setdefault(match.group(1), match.group(2))
    return cvars


# ----------------------------------------------------------------------------
# Measurement launch
# ----------------------------------------------------------------------------

def toggle_ladder(toggles: list[tuple[str, list[str]]], base_cvars: dict[str, str]) -> list[tuple[str, list[str]]]:
    """base0, toggle A, base1, toggle B, base2 ...; every base window restores the toggled cvars."""
    ladder: list[tuple[str, list[str]]] = [("base0", [])]
    for index, (label, commands) in enumerate(toggles):
        ladder.append((label, commands))
        restore = []
        for command in commands:
            name = command.split()[0]
            if name in base_cvars:
                restore.append(f"{name} {base_cvars[name]}")
        ladder.append((f"base{index + 1}", restore))
    return ladder


def build_scenario_cfg(scenario: Scenario, passes: int, duration_ms: int, attribution: bool, api: str, shots: bool, head_build: bool,
                       toggles: list[tuple[str, list[str]]] | None = None, base_cvars: dict[str, str] | None = None) -> list[str]:
    lines: list[str] = [f"echo PERF_LAUNCH_BEGIN {scenario.id}", "gfxInfo"]
    lines += list(scenario.setup)
    lines.append("wait 30")
    pass_count = scenario.passes or passes
    if toggles:
        for label, commands in toggle_ladder(toggles, base_cvars or {}):
            csv_name = f"diagnostics/{scenario.id}_{label}.csv"
            lines.append(f"echo PERF_PASS_SETUP {scenario.id} {label}")
            lines += [line.replace("{pass}", label) for line in scenario.pass_setup]
            lines += commands
            lines.append(f"waitMsec {scenario.settle_ms}")
            lines += ["framePacingReset", f"profileFrameWork 60000 {csv_name}", f"echo PERF_PASS_BEGIN {scenario.id} {label}"]
            motion = [line.replace("{pass}", label).replace("{duration}", str(duration_ms)) for line in scenario.motion]
            waited = sum(int(m.split()[1]) for m in motion if m.startswith("waitMsec "))
            motion.append(f"waitMsec {max(duration_ms + 1000 - waited, 500)}")
            lines += motion
            lines += ["profileFrameWork 0", f"framePacingSnapshot {scenario.id}_{label}", f"echo PERF_PASS_END {scenario.id} {label}"]
            lines += [line.replace("{pass}", label) for line in scenario.teardown]
        lines += [f"echo PERF_LAUNCH_END {scenario.id}", "listCvars", "wait 10", "quit"]
        return lines
    if scenario.events:
        for name, commands in scenario.events:
            if name in scenario.head_only_events and not head_build:
                continue
            csv_name = f"diagnostics/{scenario.id}_{name}.csv"
            lines += [f"echo PERF_EVENT_SETUP {name}", "waitMsec 1500", "framePacingReset", f"profileFrameWork 60000 {csv_name}",
                      "waitMsec 600", f"echo PERF_EVENT_FIRE {name}", *commands, "waitMsec 2500", "profileFrameWork 0",
                      f"framePacingSnapshot {scenario.id}_{name}", f"echo PERF_EVENT_END {name}"]
    else:
        for index in range(1, pass_count + 1):
            csv_name = f"diagnostics/{scenario.id}_p{index}.csv"
            lines.append(f"echo PERF_PASS_SETUP {scenario.id} {index}")
            lines += [line.replace("{pass}", str(index)) for line in scenario.pass_setup]
            lines.append(f"waitMsec {scenario.settle_ms}")
            if attribution:
                lines += ["r_rendererMetrics 1", "r_rendererGpuTimers 1"]
                if api == "vk":
                    lines.append("r_vkGpuPhaseProfile 1")
            lines += ["framePacingReset", f"profileFrameWork 60000 {csv_name}", f"echo PERF_PASS_BEGIN {scenario.id} {index}"]
            motion = [line.replace("{pass}", str(index)).replace("{duration}", str(duration_ms)) for line in scenario.motion]
            waited = sum(int(m.split()[1]) for m in motion if m.startswith("waitMsec "))
            motion.append(f"waitMsec {max(duration_ms + 1000 - waited, 500)}")
            lines += motion
            lines += ["profileFrameWork 0", f"framePacingSnapshot {scenario.id}_p{index}", f"echo PERF_PASS_END {scenario.id} {index}"]
            if attribution:
                lines += [f"rendererBenchmarkCapture diagnostics/{scenario.id}_p{index}_bench.csv", "r_rendererMetrics 0", "r_rendererGpuTimers 0"]
            if shots:
                lines += [f'screenshot "screenshots/perf/{scenario.id}_p{index}.tga"', "wait 3"]
            lines += [line.replace("{pass}", str(index)) for line in scenario.teardown]
    if scenario.transition_map:
        lines += [f"echo PERF_TRANSITION_BEGIN {scenario.transition_map}", f"map {scenario.transition_map}", "wait 60",
                  f"echo PERF_TRANSITION_END {scenario.transition_map}"]
    lines += [f"echo PERF_LAUNCH_END {scenario.id}", "listCvars", "wait 10", "quit"]
    return lines


def write_scenario_cfgs(game_dir: Path, cfg_name: str, lines: list[str], limit: int = 24576) -> list[Path]:
    """Write the scenario script as a chain of exec-linked files, each well below the console buffer.

    `exec` inserts a whole file into idCmdSystem's 64 KB text buffer at once; a long toggle ladder on
    the effects scene (every window spawns and removes 22 entities) is 73 KB as one file and was
    refused with "InsertText: buffer overflow". Files split only at window boundaries."""
    chunks: list[list[str]] = [[]]
    size = 0
    for line in lines:
        starts_window = line.startswith("echo PERF_PASS_SETUP") or line.startswith("echo PERF_EVENT_SETUP")
        if starts_window and size > limit and chunks[-1]:
            chunks.append([])
            size = 0
        chunks[-1].append(line)
        size += len(line) + 1
    stem = cfg_name[:-4] if cfg_name.endswith(".cfg") else cfg_name
    names = [cfg_name] + [f"{stem}_{index}.cfg" for index in range(1, len(chunks))]
    for stale in game_dir.glob(f"{stem}_*.cfg"):
        stale.unlink()
    paths: list[Path] = []
    for index, (name, chunk) in enumerate(zip(names, chunks)):
        body = chunk + ([f"exec {names[index + 1]}"] if index + 1 < len(names) else [])
        target = game_dir / name
        target.write_text("\n".join(body) + "\n", encoding="ascii")
        paths.append(target)
    return paths


class CpuStackSampler:
    """Runs tools/debug/cpu_sampler.py against the client once the first measured pass begins.

    `marker` is a regular expression matched against the log; `echo` prints every argument as its own
    token and com_timestampPrints prefixes each with [msec], so a literal marker never matches."""

    def __init__(self, log_path: Path, out_json: Path, seconds: float, marker: str,
                 symbol_dirs: "list[Path] | None" = None) -> None:
        self.log_path, self.out_json, self.seconds, self.marker = log_path, out_json, seconds, re.compile(marker)
        # The runtime's own directories go first on the sampler's symbol path: a staged module's embedded PDB path
        # names the build directory, whose PDB may since have been relinked (same GUID, new age) and then resolves
        # to exports only.
        self.symbol_dirs = [Path(d) for d in (symbol_dirs or [])]
        self.result: dict[str, Any] = {}
        self._thread: threading.Thread | None = None

    def __call__(self, proc: subprocess.Popen) -> None:
        self._thread = threading.Thread(target=self._run, args=(proc,), daemon=True)
        self._thread.start()

    def _run(self, proc: subprocess.Popen) -> None:
        deadline = time.time() + 240
        while time.time() < deadline and proc.poll() is None:
            if self.log_path.exists():
                try:
                    if self.marker.search(self.log_path.read_text(encoding="latin-1", errors="replace")):
                        break
                except OSError:
                    pass
            time.sleep(0.5)
        else:
            self.result = {"error": "measured pass never started"}
            return
        time.sleep(1.0)
        sampler = REPO_ROOT / "tools" / "debug" / "cpu_sampler.py"
        cmd = [sys.executable, str(sampler), "record", str(proc.pid), str(self.seconds), str(self.out_json), "--hz", "150"]
        env = dict(os.environ)
        if self.symbol_dirs:
            inherited = [p for p in env.get("_NT_SYMBOL_PATH", "").split(";") if p]
            env["_NT_SYMBOL_PATH"] = ";".join([str(d.resolve()) for d in self.symbol_dirs] + inherited)
        started = time.time()
        run = subprocess.run(cmd, capture_output=True, text=True, cwd=str(REPO_ROOT), env=env)
        self.result = {"seconds": self.seconds, "exit_code": run.returncode, "wall_s": round(time.time() - started, 1),
                       "stderr": run.stderr[-600:], "json": str(self.out_json)}
        if run.returncode == 0 and self.out_json.exists():
            report = subprocess.run([sys.executable, str(sampler), "report", str(self.out_json), "--top", "60"],
                                    capture_output=True, text=True, cwd=str(REPO_ROOT))
            self.out_json.with_suffix(".report.txt").write_text(report.stdout + report.stderr, encoding="utf-8")

    def wait(self) -> dict[str, Any]:
        if self._thread:
            self._thread.join(timeout=self.seconds + 120)
        return self.result


def run_measurement(cfg: RunConfig, scenario: Scenario, round_index: int, passes: int, duration_ms: int, basepath: str,
                    width: int, height: int, fullscreen: bool, hidden: bool, attribution: bool, shots: bool,
                    out_dir: Path, head_build: bool, cpu_sample_s: float = 0.0,
                    toggles: list[tuple[str, list[str]]] | None = None) -> dict[str, Any]:
    game_dir = cfg.savepath / GAME_DIR
    game_dir.mkdir(parents=True, exist_ok=True)
    name = f"{scenario.id}_r{round_index}"
    cfg_name = f"perf_{scenario.id}.cfg"
    lines = build_scenario_cfg(scenario, passes, duration_ms, attribution, cfg.api, shots, head_build, toggles, cfg.cvars)
    write_scenario_cfgs(game_dir, cfg_name, lines)
    for stale in (game_dir / "diagnostics").glob(f"{scenario.id}_*.csv"):
        stale.unlink()
    extra = list(cfg.settings.overrides) + [("g_autoExecAfterMapLoad", cfg_name), ("g_autoExecAfterMapLoadDelayMs", "1000")]
    args = common_sets(cfg, f"{name}.log", width, height, fullscreen, hidden, basepath, extra)
    args += ["+map", scenario.map]
    pass_count = (2 * len(toggles) + 1) if toggles else (scenario.passes or passes)
    # A window lasts as long as its scripted motion when that is longer than the requested duration
    # (the effects scene's twelve explosions take 29 s), so the timeout follows the longer of the two.
    scripted_ms = sum(int(m.split()[1]) for m in scenario.motion if m.startswith("waitMsec "))
    window_s = max(duration_ms, scripted_ms) / 1000.0 + scenario.settle_ms / 1000.0
    budget = 180 + pass_count * (window_s + 25) + (len(scenario.events) * 6 if scenario.events else 0) + (90 if scenario.transition_map else 0)
    sampler = LoadSampler()
    launch_dir = out_dir / cfg.label / scenario.id / f"round{round_index}"
    launch_dir.mkdir(parents=True, exist_ok=True)
    log = game_dir / "logs" / f"{name}.log"
    stack_sampler = None
    if cpu_sample_s > 0:
        marker = rf"PERF_PASS_BEGIN\s+(?:\[\d+\])?{re.escape(scenario.id)}\s+(?:\[\d+\])?1\b" if not scenario.events else r"PERF_EVENT_SETUP"
        stack_sampler = CpuStackSampler(log, launch_dir / "cpu_samples.json", min(cpu_sample_s, duration_ms / 1000.0 - 3), marker,
                                        [cfg.runtime, cfg.runtime / GAME_DIR])
    result = launch(cfg, args, timeout_s=budget, sampler=sampler, on_start=stack_sampler)
    if stack_sampler:
        result["cpu_sampler"] = stack_sampler.wait()
    text = log.read_text(encoding="latin-1", errors="replace") if log.exists() else ""
    if log.exists():
        shutil.copy2(log, launch_dir / "openq4.log")
    csvs: dict[str, str] = {}
    for produced in sorted((game_dir / "diagnostics").glob(f"{scenario.id}_*.csv")):
        target = launch_dir / produced.name
        shutil.move(str(produced), str(target))
        csvs[produced.stem[len(scenario.id) + 1:]] = str(target)
    for shot in sorted((game_dir / "screenshots" / "perf").glob(f"{scenario.id}_*.tga")) if shots else []:
        shutil.move(str(shot), str(launch_dir / shot.name))
    cvars = parse_listcvars(text)
    if cvars:
        (launch_dir / "cvars.txt").write_text("".join(f"{k} {v}\n" for k, v in sorted(cvars.items())), encoding="utf-8")
    record = {
        "label": cfg.label, "runtime": str(cfg.runtime), "exe_sha256": cfg.exe_sha256, "api": cfg.api,
        "key_cvars": {k: cvars.get(k) for k in KEY_CVARS if k in cvars},
        "settings": cfg.settings.name, "scenario": scenario.id, "map": scenario.map, "round": round_index,
        "version": text.splitlines()[0].strip() if text else "", "launch": result,
        "parsed": parse_log(text, scenario),
        "csv": csvs,
        "load": sampler.summary(),
        "timestamp": dt.datetime.now().astimezone().isoformat(timespec="seconds"),
    }
    (launch_dir / "result.json").write_text(json.dumps(record, indent=1), encoding="utf-8")
    return record


SNAPSHOT_RE = re.compile(
    r"Frame pacing snapshot(?:\s+(?P<label>[^(]*?))?\s*\((?P<mode>\w+)\): bound=(?P<bound>[^,]+), samples=(?P<samples>\d+), "
    r"present=(?P<present>[\d.]+) ms \((?P<hz>[\d.]+) Hz\), p50=(?P<p50>[\d.]+) ms, p95=(?P<p95>[\d.]+) ms, "
    r"p99=(?P<p99>[\d.]+) ms, max=(?P<max>[\d.]+) ms")
LOAD_RE = re.compile(r"(\d+) msec to load ([^\s]+)")
MODE_RE = re.compile(r"MODE:\s*([^,\r\n]+),\s*(\d+)\s+x\s+(\d+)\s+(windowed|borderless|fullscreen)", re.IGNORECASE)
SWEEP_RE = re.compile(r"benchmarkViewSweep: complete, ([\d.]+) degrees over (\d+) ms")
RENDERER_RE = re.compile(r"(GL_RENDERER:.*|Vulkan: driver .*|GL_VERSION:.*|Vulkan: selected .*)")
PHASE_RE = re.compile(r"Frame work phase=(\w+) samples=(\d+) mean_ms=([\d.]+) p95_ms=([\d.]+) p99_ms=([\d.]+) max_ms=([\d.]+)")
# `echo` prints every argument as its own token and com_timestampPrints prefixes each token with [msec].
MARKER_RE = re.compile(r"PERF_(PASS|EVENT)_(SETUP|BEGIN|END|FIRE)\s+(?:\[\d+\])?([A-Za-z0-9_]+)(?:\s+(?:\[\d+\])?([A-Za-z0-9_]+))?")


def window_key(match: "re.Match[str]") -> str:
    """The trace key of a marker: p<n> for numbered passes, the label for toggle windows, the name for events."""
    if match.group(1) == "EVENT":
        return match.group(3)
    label = match.group(4) or ""
    return f"p{label}" if label.isdigit() else label


def pass_order(launch_dir: Path) -> dict[str, int]:
    """Position of every measured window in its launch, from the log (toggle ladders are not alphabetical)."""
    log = launch_dir / "openq4.log"
    if not log.exists():
        return {}
    order: dict[str, int] = {}
    for m in MARKER_RE.finditer(log.read_text(encoding="latin-1", errors="replace")):
        if m.group(2) == "BEGIN" or (m.group(1) == "EVENT" and m.group(2) == "FIRE"):
            order.setdefault(window_key(m), len(order))
    return order


def top_warnings(lines: list[str], limit: int = 8) -> list[list[Any]]:
    counts: dict[str, int] = {}
    for line in lines:
        if "WARNING:" in line:
            key = re.sub(r"^\[\d+\]", "", line).strip()
            key = re.sub(r"\d+", "#", key)[:140]
            counts[key] = counts.get(key, 0) + 1
    return [[n, k] for k, n in sorted(counts.items(), key=lambda kv: -kv[1])[:limit]]


def parse_log(text: str, scenario: Scenario) -> dict[str, Any]:
    snapshots = []
    for m in SNAPSHOT_RE.finditer(text):
        entry = {k: (float(v) if k in ("present", "hz", "p50", "p95", "p99", "max") else v) for k, v in m.groupdict().items() if v is not None}
        entry["samples"] = int(m.group("samples"))
        snapshots.append(entry)
    loads = [{"msec": int(a), "what": b} for a, b in LOAD_RE.findall(text)]
    mode = MODE_RE.search(text)
    lines = text.splitlines()
    pass_markers = {}
    for i, line in enumerate(lines):
        m = MARKER_RE.search(line)
        if m:
            pass_markers.setdefault(window_key(m), {})[m.group(2).lower()] = i
    late_loads = {}
    for key, marks in pass_markers.items():
        if "begin" in marks and "end" in marks:
            segment = lines[marks["begin"]:marks["end"]]
            late_loads[key] = sum(1 for l in segment if "non pre-cached" in l or "Loading non" in l)
    return {
        "snapshots": snapshots,
        "loads": loads,
        "mode": {"r_mode": mode.group(1).strip(), "width": int(mode.group(2)), "height": int(mode.group(3)), "kind": mode.group(4).lower()} if mode else None,
        "sweeps": [{"degrees": float(a), "ms": int(b)} for a, b in SWEEP_RE.findall(text)],
        "renderer": [m.group(1).strip() for m in RENDERER_RE.finditer(text)][:6],
        "frame_work_phases": [{"phase": p, "samples": int(n), "mean": float(a), "p95": float(b), "p99": float(c), "max": float(d)} for p, n, a, b, c, d in PHASE_RE.findall(text)],
        "late_loads_per_window": late_loads,
        "warnings": sum(1 for l in lines if "WARNING:" in l),
        "warnings_top": top_warnings(lines),
        "errors": [l.strip()[:200] for l in lines if "ERROR:" in l or "FATAL" in l][:20],
        "level_load_lines": [l.strip()[:200] for l in lines if "Map load phases" in l or "Map media phases" in l or "Game media" in l][:12],
    }


# ----------------------------------------------------------------------------
# Frame-time statistics
# ----------------------------------------------------------------------------

def read_frame_intervals(path: Path) -> tuple[list[float], list[dict[str, float]]]:
    rows: list[dict[str, float]] = []
    with path.open(newline="") as handle:
        for row in csv.DictReader(handle):
            try:
                rows.append({k: float(v) for k, v in row.items()})
            except (TypeError, ValueError):
                continue
    intervals: list[float] = []
    for a, b in zip(rows, rows[1:]):
        if "startMs" in a and "startMs" in b and b["startMs"] > a["startMs"]:
            intervals.append(b["startMs"] - a["startMs"])
        else:
            intervals.append(a.get("totalMs", 0.0) + a.get("gapMs", 0.0))
    return intervals, rows


def percentile(sorted_values: list[float], fraction: float) -> float:
    if not sorted_values:
        return float("nan")
    index = min(len(sorted_values) - 1, max(0, int(math.ceil(len(sorted_values) * fraction)) - 1))
    return sorted_values[index]


def frame_stats(intervals: list[float], rows: list[dict[str, float]] | None = None) -> dict[str, float]:
    if len(intervals) < 2:
        return {}
    ordered = sorted(intervals)
    mean = statistics.fmean(intervals)
    if mean <= 0.0:
        return {}
    median = ordered[len(ordered) // 2]
    slowest_1pct = ordered[max(1, int(len(ordered) * 0.01)) * -1:]
    slowest_01pct = ordered[max(1, int(len(ordered) * 0.001)) * -1:]
    hitches = [i for i in intervals if i > 2.0 * median]
    stats = {
        "frames": len(intervals),
        "seconds": round(sum(intervals) / 1000.0, 2),
        "mean_ms": round(mean, 3), "fps_avg": round(1000.0 / mean, 1),
        "median_ms": round(median, 3),
        "p95_ms": round(percentile(ordered, 0.95), 3),
        "p99_ms": round(percentile(ordered, 0.99), 3), "fps_1pct_low": round(1000.0 / percentile(ordered, 0.99), 1),
        "p999_ms": round(percentile(ordered, 0.999), 3), "fps_01pct_low": round(1000.0 / percentile(ordered, 0.999), 1),
        "fps_1pct_low_avg": round(1000.0 / statistics.fmean(slowest_1pct), 1),
        "fps_01pct_low_avg": round(1000.0 / statistics.fmean(slowest_01pct), 1),
        "max_ms": round(ordered[-1], 3),
        "stdev_ms": round(statistics.pstdev(intervals), 3),
        "cv_pct": round(100.0 * statistics.pstdev(intervals) / mean, 1),
        "hitch_frames": len(hitches),
        "hitch_ms_total": round(sum(hitches), 1),
    }
    if rows:
        # Mean main-thread phase split of the frame (the profileFrameWork columns): the game tic and
        # presentation run in `session`, the renderer front end, backend and swap in `draw`.
        for column, name in (("sessionMs", "session_ms"), ("drawMs", "draw_ms"), ("waitMs", "wait_ms"), ("eventsMs", "events_ms")):
            values = [r[column] for r in rows if column in r]
            if values:
                stats[name] = round(statistics.fmean(values), 3)
    return stats


def hitch_list(intervals: list[float], rows: list[dict[str, float]], limit: int = 50) -> list[dict[str, float]]:
    if len(intervals) < 2:
        return []
    median = sorted(intervals)[len(intervals) // 2]
    t = 0.0
    hitches = []
    for i, interval in enumerate(intervals):
        if interval > 2.0 * median:
            entry = {"t_s": round(t / 1000.0, 3), "ms": round(interval, 2), "frame": int(rows[i].get("frame", i))}
            for key in ("gapMs", "waitMs", "eventsMs", "configurationMs", "commandsMs", "sessionMs", "drawMs", "tailMs"):
                if key in rows[i]:
                    entry[key] = round(rows[i][key], 2)
            hitches.append(entry)
        t += interval
    hitches.sort(key=lambda h: -h["ms"])
    return hitches[:limit]


# ----------------------------------------------------------------------------
# Commands
# ----------------------------------------------------------------------------

def parse_config_arg(text: str) -> RunConfig:
    label, _, rest = text.partition("=")
    if not rest:
        raise SystemExit(f"--config needs LABEL=RUNTIME:API:SETTINGS, got {text!r}")
    parts = rest.rsplit(":", 2)
    if len(parts) != 3:
        raise SystemExit(f"--config needs LABEL=RUNTIME:API:SETTINGS, got {text!r}")
    runtime, api, settings = parts
    if api not in ("gl", "vk"):
        raise SystemExit(f"api must be gl or vk in {text!r}")
    profile = settings_from_spec(settings)
    path = Path(runtime)
    if not path.is_absolute():
        path = (REPO_ROOT / path).resolve()
    if not (path / CLIENT_EXE).is_file():
        raise SystemExit(f"runtime {path} has no {CLIENT_EXE}")
    return RunConfig(label=label, runtime=path, api=api, settings=profile)


SETTINGS_SPEC_PRESET_RE = re.compile(r"[a-z][a-z0-9]*")
SETTINGS_SPEC_CVAR_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
SETTINGS_SPEC_VALUE_RE = re.compile(r"[-A-Za-z0-9_.]+")


def settings_from_spec(spec: str) -> Settings:
    """A named profile, or an ad-hoc `<preset>[+cvar=value...]` recipe (e.g. `quality+r_multiSamples=2`)
    for screening preset candidates without a rebuild; the preset is applied, then the overrides."""
    if spec in SETTINGS:
        return SETTINGS[spec]
    preset, *pairs = spec.split("+")
    if not SETTINGS_SPEC_PRESET_RE.fullmatch(preset):
        raise SystemExit(f"unknown settings profile {spec!r}; choose from {', '.join(SETTINGS)} or <preset>[+cvar=value...]")
    overrides: list[tuple[str, str]] = []
    for pair in pairs:
        name, sep, value = pair.partition("=")
        if not sep or not SETTINGS_SPEC_CVAR_RE.fullmatch(name) or not SETTINGS_SPEC_VALUE_RE.fullmatch(value):
            raise SystemExit(f"settings spec {spec!r}: {pair!r} is not cvar=value")
        overrides.append((name, value))
    described = f"the build's {preset} preset" + (f" with {len(overrides)} overrides" if overrides else "")
    return Settings(spec, preset, tuple(overrides), described + ", frame cap and vsync off")


def select_scenarios(spec: str) -> list[Scenario]:
    if spec == "core":
        ids = list(CORE_SET)
    elif spec == "timed":
        ids = list(ALL_TIMED)
    elif spec == "all":
        ids = list(SCENARIOS)
    else:
        ids = [s.strip() for s in spec.split(",") if s.strip()]
    missing = [i for i in ids if i not in SCENARIOS]
    if missing:
        raise SystemExit(f"unknown scenarios: {missing}; known: {', '.join(SCENARIOS)}")
    return [SCENARIOS[i] for i in ids]


def cmd_list(_: argparse.Namespace) -> int:
    print("Scenarios:")
    for s in SCENARIOS.values():
        print(f"  {s.id:22s} [{s.category:10s}] {s.map:18s} {s.purpose}")
        print(f"  {'':22s} area: {s.area}")
    print("\nSettings profiles:")
    for s in SETTINGS.values():
        print(f"  {s.name:16s} preset={s.preset!s:8s} overrides={len(s.overrides):2d}  {s.description}")
    return 0


def cmd_run(args: argparse.Namespace) -> int:
    configs = [parse_config_arg(c) for c in args.config]
    scenarios = select_scenarios(args.scenarios)
    out_dir = Path(args.output) if args.output else REPO_ROOT / ".tmp" / "perf-suite" / dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = out_dir if out_dir.is_absolute() else (REPO_ROOT / out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    basepath = args.basepath if args.basepath is not None else default_basepath()
    width, height = args.width, args.height
    fullscreen = not args.windowed and not args.hidden
    for cfg in configs:
        cfg.savepath = out_dir / cfg.label / "save"
        cfg.exe_sha256 = sha256_file(cfg.runtime / CLIENT_EXE)
        cfg.has_frame_logger = b"profileFrameWork" in (cfg.runtime / CLIENT_EXE).read_bytes()
        cfg.has_head_cheats = is_head_build(cfg)
    plan = {
        "created": dt.datetime.now().astimezone().isoformat(timespec="seconds"),
        "configs": [{"label": c.label, "runtime": str(c.runtime), "api": c.api, "settings": c.settings.name, "exe_sha256": c.exe_sha256,
                     "frame_logger": c.has_frame_logger, "head_cheats": c.has_head_cheats} for c in configs],
        "scenarios": [s.id for s in scenarios], "rounds": args.rounds, "passes": args.passes, "duration_ms": args.duration_ms,
        "display": {"width": width, "height": height, "fullscreen": fullscreen, "hidden": args.hidden},
        "basepath": basepath, "attribution": args.attribution, "host": host_identity(),
    }
    (out_dir / "plan.json").write_text(json.dumps(plan, indent=1), encoding="utf-8")
    if args.dry_run:
        for s in scenarios:
            print(f"--- {s.id} cfg ---")
            print("\n".join(build_scenario_cfg(s, args.passes, args.duration_ms, args.attribution, configs[0].api, args.shots, True,
                                                parse_toggles(args.toggle), {})))
        return 0
    summary_path = out_dir / "summary.jsonl"
    toggles = parse_toggles(args.toggle)
    for cfg in configs:
        info = ensure_config(cfg, basepath, width, height, fullscreen, args.hidden, out_dir, args.reconfigure)
        cfg.cvars = info.get("cvars", {})
        print(f"[config] {cfg.label}: {info.get('version')!r} preset={info.get('preset')} config_written={info.get('config_written')} cvars={len(cfg.cvars)}", flush=True)
    for round_index in range(1, args.rounds + 1):
        for scenario in scenarios:
            for cfg in configs:
                record = run_measurement(cfg, scenario, round_index, args.passes, args.duration_ms, basepath, width, height,
                                         fullscreen, args.hidden, args.attribution, args.shots, out_dir, cfg.has_head_cheats,
                                         args.cpu_sample, toggles)
                retries = 0
                while launch_suspect(record) and retries < args.max_retries:
                    # The launch did not render (see launch_suspect): keep its files aside and launch it again.
                    retries += 1
                    launch_dir = out_dir / cfg.label / scenario.id / f"round{round_index}"
                    quarantine = launch_dir.with_name(f"round{round_index}_suspect{retries}")
                    if quarantine.exists():
                        shutil.rmtree(quarantine)
                    shutil.move(str(launch_dir), str(quarantine))
                    record["quarantine"] = str(quarantine)
                    with (out_dir / "suspects.jsonl").open("a", encoding="utf-8") as handle:
                        handle.write(json.dumps(record) + "\n")
                    print(f"[retry {retries}/{args.max_retries}] {scenario.id} {cfg.label} r{round_index}: {launch_suspect(record)}", flush=True)
                    time.sleep(SUSPECT_RETRY_SETTLE_S)
                    record = run_measurement(cfg, scenario, round_index, args.passes, args.duration_ms, basepath, width, height,
                                             fullscreen, args.hidden, args.attribution, args.shots, out_dir, cfg.has_head_cheats,
                                             args.cpu_sample, toggles)
                record["retries"] = retries
                with summary_path.open("a", encoding="utf-8") as handle:
                    handle.write(json.dumps(record) + "\n")
                snaps = record["parsed"]["snapshots"]
                brief = " | ".join(f"{s.get('label', '?')}: {s['hz']:.0f} Hz p99 {s['p99']:.1f}" for s in snaps) or "no snapshot"
                flags = (" TIMEOUT" if record["launch"]["timed_out"] else "") + (f" RETRIED x{retries}" if retries else "") \
                    + (" SUSPECT" if launch_suspect(record) else "")
                print(f"[r{round_index}] {scenario.id:22s} {cfg.label:12s} {brief} (wall {record['launch']['wall_s']}s){flags}", flush=True)
    print(f"done: {out_dir}")
    return 0


def parse_toggles(items: list[str]) -> list[tuple[str, list[str]]]:
    """--toggle LABEL=cmd1;cmd2 -> [(label, [cmd1, cmd2])]; labels must be console-safe (no '-')."""
    toggles = []
    for item in items:
        label, _, rest = item.partition("=")
        if not rest or not re.fullmatch(r"[A-Za-z0-9_]+", label):
            raise SystemExit(f"--toggle needs LABEL=cmd1;cmd2 with an alphanumeric label, got {item!r}")
        toggles.append((label, [c.strip() for c in rest.split(";") if c.strip()]))
    return toggles


def is_head_build(cfg: RunConfig) -> bool:
    """True when the runtime's game module has the HEAD-only hitch triggers."""
    dll = cfg.runtime / GAME_DIR / "game-sp_x64.dll"
    if not dll.is_file():
        return False
    data = dll.read_bytes()
    return b"testWeaponScope" in data


def host_identity() -> dict[str, Any]:
    info: dict[str, Any] = {"platform": sys.platform, "python": sys.version.split()[0]}
    try:
        out = subprocess.run(["nvidia-smi", "--query-gpu=name,driver_version,memory.total", "--format=csv,noheader"],
                             capture_output=True, text=True, timeout=10).stdout.strip()
        info["gpu"] = out
    except Exception:
        pass
    if os.name == "nt":
        try:
            out = subprocess.run(["powershell.exe", "-NoProfile", "-Command",
                                  "(Get-CimInstance Win32_Processor).Name; (Get-CimInstance Win32_OperatingSystem).Version; "
                                  "[math]::Round((Get-CimInstance Win32_PhysicalMemory | Measure-Object Capacity -Sum).Sum/1GB)"],
                                 capture_output=True, text=True, timeout=30).stdout.split("\n")
            parts = [p.strip() for p in out if p.strip()]
            if len(parts) >= 3:
                info.update({"cpu": parts[0], "os": parts[1], "ram_gb": parts[2]})
        except Exception:
            pass
    return info


# ----------------------------------------------------------------------------
# Report
# ----------------------------------------------------------------------------

def load_records(run_dirs: list[Path], name: str = "summary.jsonl") -> list[dict[str, Any]]:
    """The run's launch records; `suspects.jsonl` holds the launches the run retried (files under round<N>_suspect<k>)."""
    records = []
    for run_dir in run_dirs:
        summary = run_dir / name
        if summary.exists():
            for line in summary.read_text(encoding="utf-8").splitlines():
                if line.strip():
                    rec = json.loads(line)
                    rec["_run_dir"] = str(run_dir)
                    records.append(rec)
    return records


def analyse(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    rows = []
    for rec in records:
        order: dict[str, int] | None = None
        for key, path in rec.get("csv", {}).items():
            p = Path(path)
            if not p.exists() or key.endswith("_bench"):
                continue
            intervals, raw = read_frame_intervals(p)
            stats = frame_stats(intervals, raw)
            if not stats:
                continue
            if order is None:
                order = pass_order(p.parent)
            snapshot = next((s for s in rec["parsed"]["snapshots"] if s.get("label", "").endswith(key)), None)
            rows.append({
                "label": rec["label"], "api": rec["api"], "settings": rec["settings"], "scenario": rec["scenario"],
                "round": rec["round"], "pass": key, "cold": key in ("p1",) or key.startswith("p1_") or rec["scenario"].startswith("hitch"),
                "order": order.get(key, 999), "exe": rec.get("exe_sha256", ""),
                "stats": stats, "snapshot": snapshot, "hitches": hitch_list(intervals, raw), "csv": path,
                "load": rec.get("load", {}), "version": rec.get("version", ""),
            })
    return rows


def aggregate(rows: list[dict[str, Any]]) -> dict[tuple[str, str, str, str], dict[str, Any]]:
    groups: dict[tuple[str, str, str, str], list[dict[str, Any]]] = {}
    for row in rows:
        kind = "cold" if row["cold"] else ("warm" if re.fullmatch(r"p\d+", row["pass"]) else row["pass"])
        groups.setdefault((row["scenario"], row["label"], row["api"], kind), []).append(row)
    result = {}
    for key, members in groups.items():
        def med(name: str) -> float:
            return round(statistics.median(m["stats"][name] for m in members), 2)
        result[key] = {
            "passes": len(members), "fps_avg": med("fps_avg"), "fps_1pct_low": med("fps_1pct_low"), "fps_01pct_low": med("fps_01pct_low"),
            "p99_ms": med("p99_ms"), "p999_ms": med("p999_ms"), "max_ms": round(max(m["stats"]["max_ms"] for m in members), 2),
            "mean_ms": med("mean_ms"), "stdev_ms": med("stdev_ms"), "cv_pct": med("cv_pct"),
            "hitch_frames": round(statistics.median(m["stats"]["hitch_frames"] for m in members), 1),
            "session_ms": round(statistics.median(m["stats"].get("session_ms", 0.0) for m in members), 3),
            "draw_ms": round(statistics.median(m["stats"].get("draw_ms", 0.0) for m in members), 3),
            "spread_fps_avg": [round(min(m["stats"]["fps_avg"] for m in members), 1), round(max(m["stats"]["fps_avg"] for m in members), 1)],
            "cpu_mean": statistics.median([m["load"].get("cpu_mean", 0.0) for m in members]) if members else 0.0,
            "gpu_mean": statistics.median([m["load"]["gpu_mean"] for m in members if "gpu_mean" in m["load"]] or [0.0]),
            "gpu_mem_max": max([m["load"]["gpu_mem_max"] for m in members if "gpu_mem_max" in m["load"]] or [0.0]),
            "exe": members[0]["exe"], "settings": members[0]["settings"],
        }
    return result


def comparison_lines(groups: dict[tuple[str, str, str, str], dict[str, Any]], baseline: str | None,
                     target_avg: float, target_low: float) -> list[str]:
    """Per scenario, API and pass kind: every build against the baseline build, the settings/code split and target factors."""
    labels = sorted({k[1] for k in groups})
    if not labels:
        return []
    base = baseline or next((l for l in labels if l.startswith("v0132")), labels[0])
    exe_of = {k[1]: g["exe"] for k, g in groups.items()}
    settings_of = {k[1]: g["settings"] for k, g in groups.items()}
    lines = ["## Build comparison", "",
             f"Deltas are against `{base}`. Avg fps = 1000 / mean frame time, 1% low = 1000 / p99, 0.1% low = 1000 / p99.9, "
             f"each the median over the passes of the group; worst = the longest frame of any pass. The last column is the "
             f"speed-up the build still needs for {target_avg:.0f} fps average / {target_low:.0f} fps 1% low.", ""]
    for api in sorted({k[2] for k in groups}):
        for scenario in sorted({k[0] for k in groups if k[2] == api}):
            for kind in ("cold", "warm"):
                cells = {l: groups[(scenario, l, api, kind)] for l in labels if (scenario, l, api, kind) in groups}
                if not cells:
                    continue
                b = cells.get(base)
                lines += [f"### {scenario} / {api} / {kind}", "",
                          "| build | passes | avg fps | 1% low | 0.1% low | mean ms | session ms | draw ms | p99 ms | worst ms | stdev ms | CV % | CPU % | GPU % | VRAM MiB | avg vs base | 1% low vs base | needs |",
                          "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
                for label, g in cells.items():
                    d_avg = d_low = ""
                    if b and label != base and b["fps_avg"] and b["fps_1pct_low"]:
                        d_avg = f"{100.0 * (g['fps_avg'] / b['fps_avg'] - 1):+.1f}%"
                        d_low = f"{100.0 * (g['fps_1pct_low'] / b['fps_1pct_low'] - 1):+.1f}%"
                    need = f"x{target_avg / g['fps_avg']:.2f} / x{target_low / g['fps_1pct_low']:.2f}" if g["fps_avg"] and g["fps_1pct_low"] else ""
                    vram = f"{g['gpu_mem_max']:.0f}" if g.get("gpu_mem_max") else "-"
                    gpu = f"{g['gpu_mean']:.0f}" if g.get("gpu_mean") else "-"
                    lines.append(f"| {label} | {g['passes']} | {g['fps_avg']:.1f} ({g['spread_fps_avg'][0]:.0f}-{g['spread_fps_avg'][1]:.0f}) | {g['fps_1pct_low']:.1f} | {g['fps_01pct_low']:.1f} | "
                                 f"{g['mean_ms']:.2f} | {g['session_ms']:.2f} | {g['draw_ms']:.2f} | {g['p99_ms']:.2f} | {g['max_ms']:.1f} | {g['stdev_ms']:.2f} | {g['cv_pct']:.1f} | {g['cpu_mean']:.0f} | {gpu} | {vram} | {d_avg} | {d_low} | {need} |")
                lines.append("")
                if b:
                    # A build measured at the baseline's settings (eq-*) isolates its code from its own preset.
                    for eq_label, eq in cells.items():
                        if eq_label == base or not settings_of.get(eq_label, "").startswith("eq-"):
                            continue
                        own = next((l for l, h in cells.items() if l not in (base, eq_label) and exe_of.get(l) == exe_of.get(eq_label)
                                    and settings_of.get(l) == settings_of.get(base)), None)
                        code = eq["mean_ms"] - b["mean_ms"]
                        phases = f" Phase split of the code share: session {eq['session_ms'] - b['session_ms']:+.2f} ms, draw {eq['draw_ms'] - b['draw_ms']:+.2f} ms."
                        if own:
                            o = cells[own]
                            lines.append(f"Gap `{own}` - `{base}`: {o['mean_ms'] - b['mean_ms']:+.2f} ms/frame = settings {o['mean_ms'] - eq['mean_ms']:+.2f} ms "
                                         f"(`{own}` - `{eq_label}`) + code {code:+.2f} ms (`{eq_label}` - `{base}`, {100.0 * code / b['mean_ms']:+.1f}%)." + phases)
                        else:
                            lines.append(f"Code share `{eq_label}` - `{base}`: {code:+.2f} ms/frame ({100.0 * code / b['mean_ms']:+.1f}%)." + phases)
                        lines.append("")
    return lines


def ladder_lines(rows: list[dict[str, Any]]) -> list[str]:
    """Feature ladders: each toggle window against the mean of its neighbouring base windows in the same launch."""
    windows = [r for r in rows if not re.fullmatch(r"p\d+", r["pass"]) and not r["scenario"].startswith("hitch")]
    if not windows:
        return []
    launches: dict[tuple[str, str, str, int], list[dict[str, Any]]] = {}
    for r in windows:
        launches.setdefault((r["api"], r["scenario"], r["label"], r["round"]), []).append(r)
    lines = ["## Feature ladders (one launch, windows in order; each toggle against the mean of its neighbouring base windows)", ""]
    for key in sorted(launches):
        seq = sorted(launches[key], key=lambda r: r["order"])
        bases = [r for r in seq if r["pass"].startswith("base")]
        lines += [f"### {key[1]} / {key[0]} / {key[2]} round {key[3]}", "",
                  "base windows (mean ms): " + ", ".join(f"{r['pass']} {r['stats']['mean_ms']:.2f}" for r in bases), "",
                  "| window | mean ms | session ms | draw ms | avg fps | 1% low | p99 ms | worst ms | delta ms vs bases | delta % | fps gain |",
                  "|---|---|---|---|---|---|---|---|---|---|---|"]
        for r in seq:
            s = r["stats"]
            if r["pass"].startswith("base"):
                continue
            before = [b for b in bases if b["order"] < r["order"]]
            after = [b for b in bases if b["order"] > r["order"]]
            neighbours = ([before[-1]] if before else []) + ([after[0]] if after else [])
            if neighbours:
                ref = statistics.fmean(n["stats"]["mean_ms"] for n in neighbours)
                delta = s["mean_ms"] - ref
                gain = 1000.0 / s["mean_ms"] - 1000.0 / ref
                lines.append(f"| {r['pass']} | {s['mean_ms']:.2f} | {s.get('session_ms', 0.0):.2f} | {s.get('draw_ms', 0.0):.2f} | {s['fps_avg']:.1f} | {s['fps_1pct_low']:.1f} | {s['p99_ms']:.2f} | {s['max_ms']:.1f} | {delta:+.2f} | {100.0 * delta / ref:+.1f}% | {gain:+.1f} |")
            else:
                lines.append(f"| {r['pass']} | {s['mean_ms']:.2f} | {s.get('session_ms', 0.0):.2f} | {s.get('draw_ms', 0.0):.2f} | {s['fps_avg']:.1f} | {s['fps_1pct_low']:.1f} | {s['p99_ms']:.2f} | {s['max_ms']:.1f} | | | |")
        lines.append("")
    return lines


IDLE_GPU_PERCENT = 20.0


def launch_suspect(rec: dict[str, Any]) -> str:
    """Why a launch's figures should not be trusted, or an empty string. A measured launch whose GPU stayed idle
    (mean utilization below IDLE_GPU_PERCENT over at least 30 samples) was not the foreground fullscreen window:
    the engine then draws nothing and the present paces the frame at roughly 45 Hz, whatever the settings."""
    ld = rec.get("load", {})
    gpu = ld.get("gpu_mean")
    if "r_hiddenWindow" in rec.get("launch", {}).get("command", []):
        return ""  # a hidden window presents at 60 Hz with little GPU load by design
    if isinstance(gpu, (int, float)) and ld.get("gpu_samples", 0) >= 30 and gpu < IDLE_GPU_PERCENT and rec.get("csv"):
        return f"GPU idle ({gpu:.0f}% mean): the client was not the foreground fullscreen window; frames are present pacing, not rendering"
    return ""


def warning_lines(records: list[dict[str, Any]], dropped: list[dict[str, Any]] | None = None,
                  retried: list[dict[str, Any]] | None = None) -> list[str]:
    suspects = [(rec, launch_suspect(rec)) for rec in records]
    suspects = [(rec, why) for rec, why in suspects if why]
    if not suspects and not dropped and not retried:
        return []

    def item(rec: dict[str, Any]) -> str:
        return f"- `{rec['label']}` {rec['api']} {rec['scenario']} round {rec['round']}: {launch_suspect(rec)}."

    lines = ["## Warnings", ""]
    if suspects:
        lines += ["Launches whose figures appear in the tables below but should not be trusted:", ""]
        lines += [item(rec) for rec, _ in suspects] + [""]
    if dropped:
        lines += ["Launches left out of this report (`--drop-suspect`):", ""] + [item(rec) for rec in dropped] + [""]
    if retried:
        lines += ["Launches the run repeated (the first attempt's files are under `round<N>_suspect<k>`):", ""]
        lines += [item(rec) for rec in retried] + [""]
    return lines


def launch_load_lines(records: list[dict[str, Any]]) -> list[str]:
    lines = ["## Launch load (system CPU and GPU sampled once a second over the whole launch)", "",
             "| build | api | scenario | round | wall s | exit | CPU % mean/max | GPU % mean/max | GPU MHz mean | GPU C max | GPU W mean | VRAM MiB max | note |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for rec in records:
        ld = rec.get("load", {})
        launch = rec.get("launch", {})
        exit_text = f"{launch.get('exit_code')}{' TIMEOUT' if launch.get('timed_out') else ''}"

        def f(key: str, fmt: str = "{:.0f}") -> str:
            return fmt.format(ld[key]) if isinstance(ld.get(key), (int, float)) else "-"

        lines.append(f"| {rec['label']} | {rec['api']} | {rec['scenario']} | {rec['round']} | {launch.get('wall_s', '')} | {exit_text} | "
                     f"{f('cpu_mean')}/{f('cpu_max')} | {f('gpu_mean')}/{f('gpu_max')} | {f('gpu_mhz_mean')} | {f('gpu_c_max')} | {f('gpu_w_mean')} | {f('gpu_mem_max')} | {launch_suspect(rec)} |")
    lines.append("")
    return lines


METRICS_SUMMARY_RE = re.compile(
    r"rendererMetrics summary tier=(?P<tier>\S+) fe=(?P<fe>\d+)ms visibility=(?P<visibility>\d+)ms packet=(?P<packet>\d+)ms "
    r"graph=(?P<graph>\d+)ms submit=(?P<submit>\d+)ms be=(?P<be>\d+)ms present=(?P<present>\d+)ms gpu=(?P<gpu>\S+) "
    r"views=(?P<views>\d+) ents=(?P<ents>\d+) lights=(?P<lights>\d+) draws=(?P<draws>\d+)")


def attribution_lines(records: list[dict[str, Any]]) -> list[str]:
    """Renderer attribution of the --attribution launches: the 256-frame benchmark capture written after each pass
    (CPU front end / submit / backend / present and the delayed whole-frame GPU time per frame) and the periodic
    `rendererMetrics summary` lines of the launch."""
    bench_rows: list[str] = []
    metric_rows: list[str] = []
    for rec in records:
        for key, path in sorted(rec.get("csv", {}).items()):
            if not key.endswith("_bench") or not Path(path).exists():
                continue
            with Path(path).open(newline="") as handle:
                rows = [r for r in csv.DictReader(handle)]
            if not rows:
                continue

            def med(column: str, scale: float = 1.0, valid: str | None = None) -> str:
                values = []
                for r in rows:
                    if valid and r.get(valid, "0") != "1":
                        continue
                    try:
                        values.append(float(r[column]) * scale)
                    except (KeyError, ValueError):
                        pass
                return f"{statistics.median(values):.2f}" if values else "-"
            gpu_valid = sum(1 for r in rows if r.get("gpuValid") == "1")
            bench_rows.append(f"| {rec['label']} | {rec['api']} | {rec['scenario']} | {rec['round']} | {key[:-6]} | {len(rows)} | {med('cpuUs', 0.001)} | "
                              f"{med('frontEndMs')} | {med('submitMs')} | {med('backendMs')} | {med('presentMs')} | {med('swapUs', 0.001)} | "
                              f"{med('frameFenceWaitUs', 0.001)} | {med('gpuUs', 0.001, 'gpuValid')} ({gpu_valid}) |")
        launch_dir = next((Path(p).parent for p in rec.get("csv", {}).values()), None)
        log = launch_dir / "openq4.log" if launch_dir else None
        if log and log.exists():
            found = [m.groupdict() for m in METRICS_SUMMARY_RE.finditer(log.read_text(encoding="latin-1", errors="replace"))]
            if found:
                def m(name: str) -> str:
                    return f"{statistics.median(int(f[name]) for f in found):.0f}"
                gpu = [f["gpu"] for f in found if f["gpu"].endswith("ms")]
                gpu_text = f"{statistics.median(int(g[:-2]) for g in gpu):.0f}" if gpu else "-"
                metric_rows.append(f"| {rec['label']} | {rec['api']} | {rec['scenario']} | {rec['round']} | {len(found)} | {found[0]['tier']} | {m('fe')} | {m('visibility')} | "
                                   f"{m('packet')} | {m('graph')} | {m('submit')} | {m('be')} | {m('present')} | {gpu_text} | {m('views')} | {m('ents')} | {m('lights')} | {m('draws')} |")
    lines: list[str] = []
    if bench_rows:
        lines += ["## Renderer attribution: benchmark capture (median of the 256 retained frames after each pass, ms)", "",
                  "| build | api | scenario | round | pass | frames | cpu frame | front end | submit | backend | present | swap | fence wait | gpu (valid frames) |",
                  "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|", *bench_rows, ""]
    if metric_rows:
        lines += ["## Renderer attribution: rendererMetrics summaries (median over the launch, integer ms)", "",
                  "| build | api | scenario | round | summaries | tier | fe | visibility | packet | graph | submit | be | present | gpu | views | ents | lights | draws |",
                  "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|", *metric_rows, ""]
    return lines


def snapshot_lines(records: list[dict[str, Any]], baseline: str | None) -> list[str]:
    """Engine pacing snapshots per pass (`framePacingSnapshot`: present interval mean and Hz, integer-ms percentiles):
    the only timing on builds without the frame logger, and a cross-check on the others. Groups by scenario, build,
    API and pass kind; deltas against the baseline build."""
    groups: dict[tuple[str, str, str, str], list[dict[str, Any]]] = {}
    for rec in records:
        for snap in rec["parsed"].get("snapshots", []):
            label = snap.get("label", "")
            window = label.rsplit("_", 1)[-1] if "_" in label else label
            kind = "cold" if window == "p1" else ("warm" if re.fullmatch(r"p\d+", window) else window)
            groups.setdefault((rec["scenario"], rec["label"], rec["api"], kind), []).append(snap)
    if not groups:
        return []
    labels = sorted({k[1] for k in groups})
    base = baseline or next((l for l in labels if l.startswith("v0132")), labels[0])
    lines = ["## Pacing snapshot comparison (engine `framePacingSnapshot`; present = mean frame interval)", "",
             f"Deltas against `{base}`. Builds without the frame logger (the 0.13.2 release binary, unpatched bisection commits) have only these figures; percentiles are integer milliseconds there.", "",
             "| scenario | api | kind | build | passes | present ms | Hz | p50 ms | p95 ms | p99 ms | max ms | present vs base | Hz vs base |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for scenario in sorted({k[0] for k in groups}):
        for api in sorted({k[2] for k in groups if k[0] == scenario}):
            for kind in sorted({k[3] for k in groups if k[0] == scenario and k[2] == api}, key=lambda k: (k not in ("cold", "warm"), k)):
                cells = {l: groups[(scenario, l, api, kind)] for l in labels if (scenario, l, api, kind) in groups}
                b = cells.get(base)

                def med(snaps: list[dict[str, Any]], key: str) -> float:
                    return statistics.median(s[key] for s in snaps if isinstance(s.get(key), (int, float)))
                for label, snaps in cells.items():
                    present, hz = med(snaps, "present"), med(snaps, "hz")
                    d_present = d_hz = ""
                    if b and label != base:
                        bp, bh = med(b, "present"), med(b, "hz")
                        d_present = f"{present - bp:+.2f} ({100.0 * (present / bp - 1):+.1f}%)" if bp else ""
                        d_hz = f"{100.0 * (hz / bh - 1):+.1f}%" if bh else ""
                    lines.append(f"| {scenario} | {api} | {kind} | {label} | {len(snaps)} | {present:.2f} | {hz:.1f} | {med(snaps, 'p50'):.1f} | {med(snaps, 'p95'):.1f} | "
                                 f"{med(snaps, 'p99'):.1f} | {max(s['max'] for s in snaps):.1f} | {d_present} | {d_hz} |")
    lines.append("")
    return lines


def write_svg(path: Path, series: list[tuple[str, list[float]]], title: str, y_max: float | None = None) -> None:
    width, height, left, bottom, top, right = 960, 320, 60, 40, 30, 20
    plot_w, plot_h = width - left - right, height - top - bottom
    all_values = [v for _, values in series for v in values]
    if not all_values:
        return
    y_max = y_max or min(max(all_values), max(sorted(all_values)[int(len(all_values) * 0.999)] * 1.5, 1.0))
    x_max = max(len(values) for _, values in series)
    colours = ["#1f77b4", "#d62728", "#2ca02c", "#ff7f0e", "#9467bd", "#8c564b"]
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" font-family="sans-serif" font-size="12">',
             f'<rect width="{width}" height="{height}" fill="white"/>',
             f'<text x="{left}" y="18" font-size="14">{title}</text>']
    for i in range(6):
        y = top + plot_h - plot_h * i / 5
        value = y_max * i / 5
        parts.append(f'<line x1="{left}" y1="{y:.1f}" x2="{left + plot_w}" y2="{y:.1f}" stroke="#ddd"/>')
        parts.append(f'<text x="{left - 6}" y="{y + 4:.1f}" text-anchor="end">{value:.1f}</text>')
    parts.append(f'<text x="{left - 46}" y="{top + plot_h / 2:.0f}" transform="rotate(-90 {left - 46} {top + plot_h / 2:.0f})" text-anchor="middle">frame time (ms)</text>')
    parts.append(f'<text x="{left + plot_w / 2:.0f}" y="{height - 8}" text-anchor="middle">frame index</text>')
    for colour, (name, values) in zip(colours, series):
        if not values:
            continue
        step = max(1, len(values) // 2400)
        points = []
        for i in range(0, len(values), step):
            chunk = values[i:i + step]
            v = max(chunk)
            x = left + plot_w * i / max(1, x_max - 1)
            y = top + plot_h - plot_h * min(v, y_max) / y_max
            points.append(f"{x:.1f},{y:.1f}")
        parts.append(f'<polyline fill="none" stroke="{colour}" stroke-width="1" points="{" ".join(points)}"/>')
    legend_y = top + 4
    for colour, (name, _) in zip(colours, series):
        parts.append(f'<rect x="{left + plot_w - 240}" y="{legend_y - 9}" width="12" height="12" fill="{colour}"/>')
        parts.append(f'<text x="{left + plot_w - 222}" y="{legend_y + 1}">{name}</text>')
        legend_y += 16
    parts.append("</svg>")
    path.write_text("\n".join(parts), encoding="utf-8")


def cmd_report(args: argparse.Namespace) -> int:
    run_dirs = [Path(r) if Path(r).is_absolute() else REPO_ROOT / r for r in args.runs]
    out = Path(args.out) if Path(args.out).is_absolute() else REPO_ROOT / args.out
    out.mkdir(parents=True, exist_ok=True)
    records = load_records(run_dirs)
    retried = load_records(run_dirs, "suspects.jsonl")
    dropped = [rec for rec in records if launch_suspect(rec)] if args.drop_suspect else []
    if dropped:
        records = [rec for rec in records if not launch_suspect(rec)]
    rows = analyse(records)
    groups = aggregate(rows)
    (out / "passes.json").write_text(json.dumps(rows, indent=1), encoding="utf-8")
    (out / "aggregate.json").write_text(json.dumps({"|".join(k): v for k, v in groups.items()}, indent=1), encoding="utf-8")
    lines = ["# Performance suite results", ""]
    lines += warning_lines(records, dropped, retried)
    scenarios = sorted({k[0] for k in groups})
    for scenario in scenarios:
        lines += [f"## {scenario}", "", "| build | api | pass | passes | avg fps | 1% low | 0.1% low | p99 ms | worst ms | stdev ms | CV % | hitch frames | CPU % | GPU % | VRAM MiB |", "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
        for key in sorted(k for k in groups if k[0] == scenario):
            g = groups[key]
            vram = f"{g['gpu_mem_max']:.0f}" if g.get("gpu_mem_max") else "-"
            gpu = f"{g['gpu_mean']:.0f}" if g.get("gpu_mean") else "-"
            lines.append(f"| {key[1]} | {key[2]} | {key[3]} | {g['passes']} | {g['fps_avg']:.0f} ({g['spread_fps_avg'][0]:.0f}-{g['spread_fps_avg'][1]:.0f}) | {g['fps_1pct_low']:.0f} | {g['fps_01pct_low']:.0f} | {g['p99_ms']:.2f} | {g['max_ms']:.1f} | {g['stdev_ms']:.2f} | {g['cv_pct']:.1f} | {g['hitch_frames']:.0f} | {g['cpu_mean']:.0f} | {gpu} | {vram} |")
        lines.append("")
        series = []
        for label in sorted({k[1] for k in groups if k[0] == scenario}):
            for api in sorted({k[2] for k in groups if k[0] == scenario and k[1] == label}):
                candidates = [r for r in rows if r["scenario"] == scenario and r["label"] == label and r["api"] == api and not r["cold"]] or \
                             [r for r in rows if r["scenario"] == scenario and r["label"] == label and r["api"] == api]
                if candidates:
                    pick = sorted(candidates, key=lambda r: r["stats"]["fps_avg"])[len(candidates) // 2]
                    intervals, _ = read_frame_intervals(Path(pick["csv"]))
                    series.append((f"{label}/{api} r{pick['round']} {pick['pass']}", intervals))
        if series:
            svg = out / f"{scenario}.svg"
            write_svg(svg, series, f"{scenario}: frame time per frame (median warm pass per build)")
            lines += [f"![{scenario}]({svg.name})", ""]
    hitch_rows = [(r, h) for r in rows for h in r["hitches"][:10]]
    hitch_rows.sort(key=lambda rh: -rh[1]["ms"])
    lines += ["## Longest frames (frames above twice the pass median)", "", "| ms | build | api | scenario | pass | round | t (s) | frame | phase split |", "|---|---|---|---|---|---|---|---|---|"]
    for r, h in hitch_rows[:60]:
        split = ", ".join(f"{k[:-2]} {h[k]:.1f}" for k in ("waitMs", "eventsMs", "commandsMs", "sessionMs", "drawMs", "tailMs") if k in h and h[k] >= 0.5)
        lines.append(f"| {h['ms']:.1f} | {r['label']} | {r['api']} | {r['scenario']} | {r['pass']} | {r['round']} | {h['t_s']:.2f} | {h['frame']} | {split} |")
    event_rows = [r for r in rows if r["scenario"].startswith("hitch")]
    if event_rows:
        lines += ["## First-use events (hitch scenario)", "", "| build | api | event | round | median ms | max ms | max at (s) | frames > 2x median | phase split of the max |", "|---|---|---|---|---|---|---|---|---|"]
        for r in sorted(event_rows, key=lambda r: (r["scenario"], r["pass"], r["label"], r["api"], r["round"])):
            top = r["hitches"][0] if r["hitches"] else None
            split = ", ".join(f"{k[:-2]} {top[k]:.1f}" for k in ("waitMs", "eventsMs", "commandsMs", "sessionMs", "drawMs", "tailMs") if top and k in top and top[k] >= 0.5) if top else ""
            lines.append(f"| {r['label']} | {r['api']} | {r['pass']} | {r['round']} | {r['stats']['median_ms']:.2f} | {r['stats']['max_ms']:.1f} | {top['t_s'] if top else ''} | {r['stats']['hitch_frames']} | {split} |")
        lines.append("")
    load_rows: dict[tuple[str, str, str], dict[str, list[int]]] = {}
    for rec in records:
        for item in rec["parsed"]["loads"]:
            if item["what"].startswith("game/") or item["what"].startswith("maps/"):
                key = (rec["label"], rec["api"], item["what"])
                load_rows.setdefault(key, {"cold": [], "warm": []})["cold" if rec["round"] == 1 else "warm"].append(item["msec"])
    if load_rows:
        lines += ["## Level loads (msec to load, engine timer)", "", "| build | api | map | cold (round 1) | warm (later rounds, median) | warm samples |", "|---|---|---|---|---|---|"]
        for key in sorted(load_rows):
            v = load_rows[key]
            cold = ", ".join(str(x) for x in v["cold"]) or "-"
            warm = f"{statistics.median(v['warm']):.0f}" if v["warm"] else "-"
            lines.append(f"| {key[0]} | {key[1]} | {key[2]} | {cold} | {warm} | {len(v['warm'])} |")
        lines.append("")
    lines += comparison_lines(groups, args.baseline, args.target_avg, args.target_low)
    lines += ladder_lines(rows)
    lines += attribution_lines(records)
    lines += snapshot_lines(records, args.baseline)
    lines += launch_load_lines(records)
    (out / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"report: {out / 'summary.md'} ({len(rows)} passes, {len(groups)} groups)")
    return 0


# ----------------------------------------------------------------------------
# Budgets
# ----------------------------------------------------------------------------

BUDGET_SCHEMA = 1
BUDGET_TOLERANCE_MEAN = 0.05   # a candidate fails when its warm mean frame time exceeds the budget by more than this
BUDGET_TOLERANCE_P99 = 0.10    # ... or its p99 frame time by more than this
TIER_ORDER = ("ultra", "quality", "enhanced", "balanced", "low", "lowpower", "minimum")


def settings_tier(settings_name: str) -> str:
    """The preset tier a settings profile measures: own-ultra -> ultra; profiles with overrides keep their own name."""
    try:
        profile = settings_from_spec(settings_name)
    except SystemExit:
        return settings_name
    if profile.preset and not profile.overrides:
        return profile.preset
    return settings_name


def budget_measurements(rows: list[dict[str, Any]]) -> dict[tuple[str, str, str], dict[str, Any]]:
    """Per (scenario, api, tier): the warm group of the aggregate (median over passes of the per-pass mean and p99), or
    the cold group for scenarios that measure one pass per launch (the walker)."""
    candidates: dict[tuple[str, str, str], list[tuple[str, str, dict[str, Any]]]] = {}
    for (scenario, label, api, kind), g in sorted(aggregate(rows).items()):
        if kind in ("warm", "cold"):
            candidates.setdefault((scenario, api, settings_tier(g["settings"])), []).append((label, kind, g))
    result: dict[tuple[str, str, str], dict[str, Any]] = {}
    for key, found in candidates.items():
        scenario, api = key[0], key[1]
        kind = "warm" if any(k == "warm" for _, k, _ in found) else "cold"
        chosen = [(label, g) for label, k, g in found if k == kind]
        if len(chosen) > 1:
            raise SystemExit(f"{scenario} / {api} / {key[2]} is measured by several builds ({', '.join(l for l, _ in chosen)}); "
                             "give budgets and checks one build per backend and tier")
        label, g = chosen[0]
        result[key] = {"scenario": scenario, "api": api, "tier": key[2], "settings": g["settings"], "label": label,
                       "pass_kind": kind, "passes": g["passes"], "mean_ms": g["mean_ms"], "p99_ms": g["p99_ms"],
                       "fps_avg": g["fps_avg"], "fps_1pct_low": g["fps_1pct_low"], "spread_fps_avg": g["spread_fps_avg"],
                       "max_ms": g["max_ms"], "gpu_mean": g["gpu_mean"], "exe": g["exe"]}
    return result


def select_labels(records: list[dict[str, Any]], labels: list[str]) -> list[dict[str, Any]]:
    if not labels:
        return records
    unknown = set(labels) - {rec["label"] for rec in records}
    if unknown:
        raise SystemExit(f"no launches for labels {sorted(unknown)}")
    return [rec for rec in records if rec["label"] in labels]


def _repo_relative(path: Path) -> str:
    try:
        return path.resolve().relative_to(REPO_ROOT).as_posix()
    except ValueError:
        return str(path)


def _tier_rank(tier: str) -> tuple[int, str]:
    return (TIER_ORDER.index(tier) if tier in TIER_ORDER else len(TIER_ORDER), tier)


def budget_markdown(doc: dict[str, Any]) -> list[str]:
    """One table per tier: every scenario's OpenGL and Vulkan budget (warm mean and p99) and the gate limits."""
    entries = doc["budgets"]
    apis = sorted({e["api"] for e in entries})
    lines = []
    for tier in sorted({e["tier"] for e in entries}, key=_tier_rank):
        header = "| Scenario | " + " | ".join(f"{api} mean ms (fps) | {api} p99 ms (1% low) | {api} gate mean / p99 ms" for api in apis) + " |"
        lines += [f"#### {tier}", "", header, "|---|" + "---:|---:|---:|" * len(apis)]
        for scenario in [s for s in ALL_TIMED if any(e["scenario"] == s and e["tier"] == tier for e in entries)] + \
                sorted({e["scenario"] for e in entries if e["tier"] == tier and e["scenario"] not in ALL_TIMED}):
            cells = []
            for api in apis:
                e = next((e for e in entries if e["scenario"] == scenario and e["api"] == api and e["tier"] == tier), None)
                if e is None:
                    cells += ["-", "-", "-"]
                    continue
                mark = "" if e["pass_kind"] == "warm" else " (single pass)"
                cells += [f"{e['mean_ms']:.2f} ({e['fps_avg']:.0f}){mark}", f"{e['p99_ms']:.2f} ({e['fps_1pct_low']:.0f})",
                          f"{e['limit_mean_ms']:.2f} / {e['limit_p99_ms']:.2f}"]
            lines.append(f"| {scenario} | " + " | ".join(cells) + " |")
        lines.append("")
    return lines


def cmd_budgets(args: argparse.Namespace) -> int:
    """Declare per-scenario budgets from accepted reference runs: the warm mean and p99 frame time of every scenario,
    backend and tier, with the gate limits `check` applies to a candidate."""
    run_dirs = [Path(r) if Path(r).is_absolute() else REPO_ROOT / r for r in args.runs]
    records = select_labels(load_records(run_dirs), args.labels)
    suspects = [rec for rec in records if launch_suspect(rec)]
    if suspects and not args.drop_suspect:
        for rec in suspects:
            print(f"suspect launch: {rec['label']} {rec['api']} {rec['scenario']} round {rec['round']}: {launch_suspect(rec)}")
        raise SystemExit("refusing to declare budgets from launches that did not render; re-run them or pass --drop-suspect")
    records = [rec for rec in records if not launch_suspect(rec)]
    measured = budget_measurements(analyse(records))
    if not measured:
        raise SystemExit("no frame traces in the given runs")
    plans = [json.loads((d / "plan.json").read_text(encoding="utf-8")) for d in run_dirs if (d / "plan.json").exists()]
    hosts = {json.dumps(p.get("host", {}), sort_keys=True) for p in plans}
    displays = {json.dumps(p.get("display", {}), sort_keys=True) for p in plans}
    if len(hosts) > 1 or len(displays) > 1:
        raise SystemExit("the runs come from different machines or display modes; budgets are declared for one reference machine")
    exes = sorted({e["exe"] for e in measured.values()})
    versions = sorted({rec.get("version", "") for rec in records if rec.get("version")})
    entries = []
    for e in sorted(measured.values(), key=lambda e: (_tier_rank(e["tier"]), e["scenario"], e["api"])):
        entries.append({**e, "limit_mean_ms": round(e["mean_ms"] * (1.0 + args.tolerance_mean), 3),
                        "limit_p99_ms": round(e["p99_ms"] * (1.0 + args.tolerance_p99), 3)})
    doc = {
        "schema": BUDGET_SCHEMA,
        "declared": dt.date.today().isoformat(),
        "machine": {"name": args.machine, **(plans[0].get("host", {}) if plans else {})},
        "display": plans[0].get("display", {}) if plans else {},
        "build": {"exe_sha256": exes, "version": versions},
        "runs": [_repo_relative(d) for d in run_dirs],
        "tolerance": {"mean": args.tolerance_mean, "p99": args.tolerance_p99},
        "method": "warm passes (pass 2 onward; the walker scene measures one pass per launch), median over launches of each pass's "
                  "mean and p99 frame interval from profileFrameWork; a candidate fails when its mean exceeds limit_mean_ms or its "
                  "p99 exceeds limit_p99_ms",
        "budgets": entries,
    }
    out = Path(args.out) if Path(args.out).is_absolute() else REPO_ROOT / args.out
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8")
    md = budget_markdown(doc)
    if args.markdown:
        md_path = Path(args.markdown) if Path(args.markdown).is_absolute() else REPO_ROOT / args.markdown
        md_path.write_text("\n".join(md) + "\n", encoding="utf-8")
    print("\n".join(md))
    print(f"budgets: {out} ({len(entries)} entries from {len(records)} launches)")
    return 0


def check_budgets(doc: dict[str, Any], measured: dict[tuple[str, str, str], dict[str, Any]]) -> list[dict[str, Any]]:
    """Every budget entry against the candidate's measurement of the same scenario, backend and tier."""
    results = []
    for b in doc["budgets"]:
        m = measured.get((b["scenario"], b["api"], b["tier"]))
        if m is None:
            results.append({**b, "status": "not measured"})
            continue
        over_mean = m["mean_ms"] > b["limit_mean_ms"]
        over_p99 = m["p99_ms"] > b["limit_p99_ms"]
        status = "fail" if over_mean or over_p99 else "pass"
        results.append({**b, "status": status, "candidate_mean_ms": m["mean_ms"], "candidate_p99_ms": m["p99_ms"],
                        "delta_mean_pct": round(100.0 * (m["mean_ms"] / b["mean_ms"] - 1.0), 1),
                        "delta_p99_pct": round(100.0 * (m["p99_ms"] / b["p99_ms"] - 1.0), 1),
                        "over": [name for name, flag in (("mean", over_mean), ("p99", over_p99)) if flag]})
    return results


def cmd_check(args: argparse.Namespace) -> int:
    """Gate a candidate run against declared budgets; exit 1 when any measured entry exceeds its limits."""
    budget_path = Path(args.budgets) if Path(args.budgets).is_absolute() else REPO_ROOT / args.budgets
    doc = json.loads(budget_path.read_text(encoding="utf-8"))
    if doc.get("schema") != BUDGET_SCHEMA:
        raise SystemExit(f"{budget_path}: budget schema {doc.get('schema')!r}, expected {BUDGET_SCHEMA}")
    run_dirs = [Path(r) if Path(r).is_absolute() else REPO_ROOT / r for r in args.runs]
    records = [rec for rec in select_labels(load_records(run_dirs), args.labels) if not launch_suspect(rec)]
    results = check_budgets(doc, budget_measurements(analyse(records)))
    plans = [json.loads((d / "plan.json").read_text(encoding="utf-8")) for d in run_dirs if (d / "plan.json").exists()]
    reference = {k: v for k, v in doc.get("machine", {}).items() if k in ("gpu", "cpu")}
    notes = []
    for plan in plans:
        host = {k: v for k, v in plan.get("host", {}).items() if k in ("gpu", "cpu")}
        if reference and host and host != reference:
            notes.append(f"candidate ran on {host}, the budgets were declared on {reference}: the gate only applies on the reference machine")
        if doc.get("display") and plan.get("display") and plan["display"] != doc["display"]:
            notes.append(f"candidate display {plan['display']} differs from the budgets' {doc['display']}")
    lines = ["| tier | scenario | api | budget mean / p99 ms | candidate mean / p99 ms | delta mean / p99 | status |", "|---|---|---|---|---|---|---|"]
    for r in results:
        if r["status"] == "not measured":
            lines.append(f"| {r['tier']} | {r['scenario']} | {r['api']} | {r['mean_ms']:.2f} / {r['p99_ms']:.2f} | - | - | not measured |")
        else:
            lines.append(f"| {r['tier']} | {r['scenario']} | {r['api']} | {r['mean_ms']:.2f} / {r['p99_ms']:.2f} | {r['candidate_mean_ms']:.2f} / {r['candidate_p99_ms']:.2f} | "
                         f"{r['delta_mean_pct']:+.1f}% / {r['delta_p99_pct']:+.1f}% | {r['status']}{' (' + ', '.join(r['over']) + ')' if r['over'] else ''} |")
    failed = [r for r in results if r["status"] == "fail"]
    missing = [r for r in results if r["status"] == "not measured"]
    summary = f"{len(results) - len(failed) - len(missing)} pass, {len(failed)} fail, {len(missing)} not measured"
    text = "\n".join(["# Performance budget check", "", f"Budgets: `{_repo_relative(budget_path)}` (declared {doc.get('declared')}). {summary}.", ""]
                     + [f"- {n}" for n in notes] + ([""] if notes else []) + lines) + "\n"
    if args.out:
        out = Path(args.out) if Path(args.out).is_absolute() else REPO_ROOT / args.out
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text, encoding="utf-8")
    print(text)
    if args.require_all and missing:
        return 1
    return 1 if failed else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("list", help="list scenarios and settings profiles").set_defaults(func=cmd_list)
    run = sub.add_parser("run", help="run the suite")
    run.add_argument("--config", action="append", required=True, metavar="LABEL=RUNTIME:API:SETTINGS", help="a build under test; repeat to interleave builds")
    run.add_argument("--scenarios", default="core", help="core | timed | all | comma-separated ids")
    run.add_argument("--rounds", type=int, default=3, help="launches per scenario and config (each launch is one cold pass plus warm passes)")
    run.add_argument("--passes", type=int, default=2, help="measured passes per launch")
    run.add_argument("--duration-ms", type=int, default=30000, help="measured window per pass")
    run.add_argument("--width", type=int, default=2560)
    run.add_argument("--height", type=int, default=1440)
    run.add_argument("--windowed", action="store_true", help="bordered window instead of fullscreen (smoke tests)")
    run.add_argument("--hidden", action="store_true", help="hidden window (script smoke tests only; capped at 60 Hz)")
    run.add_argument("--basepath", default=None, help="Quake 4 install path (default: Steam discovery)")
    run.add_argument("--output", default="", help="output directory (default .tmp/perf-suite/<timestamp>)")
    run.add_argument("--attribution", action="store_true", help="also record renderer metrics, GPU timers and the 256-frame benchmark capture (adds overhead)")
    run.add_argument("--shots", action="store_true", help="screenshot the view at the end of every pass")
    run.add_argument("--cpu-sample", type=float, default=0.0, metavar="SECONDS", help="sample the client's CPU stacks for this long during pass 1 (tools/debug/cpu_sampler.py; needs the runtime's PDBs)")
    run.add_argument("--toggle", action="append", default=[], metavar="LABEL=cmd;cmd", help="feature-cost ladder: one launch per scenario with windows base0, LABEL, base1, ... where each base window restores the toggled cvars; --duration-ms is the window length")
    run.add_argument("--reconfigure", action="store_true", help="redo the preset/config launch even if the save path already has one")
    run.add_argument("--dry-run", action="store_true", help="print the generated scenario scripts and exit")
    run.add_argument("--max-retries", type=int, default=1,
                     help="launch a measurement again when its GPU stayed idle (the client missed the foreground), at most this often")
    run.set_defaults(func=cmd_run)
    report = sub.add_parser("report", help="aggregate frame-time traces into tables and plots")
    report.add_argument("--runs", nargs="+", required=True, help="run output directories")
    report.add_argument("--out", required=True, help="report directory")
    report.add_argument("--baseline", default=None, help="label the comparison is measured against (default: the first label starting with v0132)")
    report.add_argument("--target-avg", type=float, default=220.0, help="average fps target for the headroom column")
    report.add_argument("--target-low", type=float, default=180.0, help="1%% low fps target for the headroom column")
    report.add_argument("--drop-suspect", action="store_true",
                        help="leave launches whose GPU stayed idle out of the tables (they are listed under Warnings)")
    report.set_defaults(func=cmd_report)
    budgets = sub.add_parser("budgets", help="declare per-scenario budgets (warm mean and p99 per backend and tier) from reference runs")
    budgets.add_argument("--runs", nargs="+", required=True, help="accepted reference run directories (one machine, one display mode)")
    budgets.add_argument("--out", required=True, help="budget file to write (JSON)")
    budgets.add_argument("--markdown", default="", help="also write the budget tables as Markdown")
    budgets.add_argument("--machine", required=True, help="name of the reference machine the budgets apply to")
    budgets.add_argument("--tolerance-mean", type=float, default=BUDGET_TOLERANCE_MEAN, help="allowed growth of the warm mean frame time (fraction)")
    budgets.add_argument("--tolerance-p99", type=float, default=BUDGET_TOLERANCE_P99, help="allowed growth of the p99 frame time (fraction)")
    budgets.add_argument("--drop-suspect", action="store_true", help="leave launches whose GPU stayed idle out instead of refusing")
    budgets.add_argument("--labels", nargs="+", default=[], help="only the launches of these build labels")
    budgets.set_defaults(func=cmd_budgets)
    check = sub.add_parser("check", help="gate a candidate run against declared budgets (exit 1 on a regression)")
    check.add_argument("--budgets", required=True, help="budget file written by `budgets`")
    check.add_argument("--runs", nargs="+", required=True, help="candidate run directories")
    check.add_argument("--out", default="", help="also write the verdict as Markdown")
    check.add_argument("--require-all", action="store_true", help="also fail when a budget entry was not measured")
    check.add_argument("--labels", nargs="+", default=[], help="only the launches of these build labels")
    check.set_defaults(func=cmd_check)
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
