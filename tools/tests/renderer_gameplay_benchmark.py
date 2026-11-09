#!/usr/bin/env python3
"""Run opt-in openQ4 renderer gameplay benchmark and capture cases.

Unlike renderer_validation_matrix.py, this runner enters maps. It is intended
for local, target-hardware validation where stock Quake 4 assets are available.
It launches from .install, writes isolated save/log roots under .tmp, captures
screenshots, dumps renderer benchmark metrics, and records a Markdown/JSON
report for performance triage. Every role fails closed on renderer, Vulkan
validation/call, fatal, and engine ERROR diagnostics found in its log streams.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import re
import shlex
import stat
import struct
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

VALIDATION_DIR = Path(__file__).resolve().parents[1] / "validation"
if str(VALIDATION_DIR) not in sys.path:
    sys.path.insert(0, str(VALIDATION_DIR))

from renderer_budget_contract import (  # noqa: E402
    DEFAULT_CONTRACT_PATH,
    MARKER_PREFIX as TIMING_EVIDENCE_MARKER,
    evaluate_timing_evidence,
    load_contract,
    verify_contract_binding,
    verify_recorded_evidence,
)
from generate_pbr_fixture import (  # noqa: E402
    BENCHMARK_CASE as PBR_FIXTURE_BENCHMARK_CASE,
    DEFAULT_TEXTURE_SIZE as PBR_FIXTURE_TEXTURE_SIZE,
    MATERIAL_NAME as PBR_FIXTURE_MATERIAL_NAME,
    STOCK_MAP as PBR_FIXTURE_STOCK_MAP,
    verify_fixture as verify_procedural_pbr_fixture,
)


SAFE_TIERS = ("auto", "legacy", "gl33", "gl41", "gl43", "gl45", "gl46")
GL_TIER_RUNTIME_NAMES = {
    "legacy": "LegacyGL2Compat",
    "gl33": "ModernGL33",
    "gl41": "ModernGL41",
    "gl43": "GpuDrivenGL43",
    "gl45": "LowOverheadGL45",
    "gl46": "TopGL46",
}
PRESENTATION_MAXFPS = ("0", "120", "240")
PRESENTATION_SWAP_INTERVALS = ("0", "1")
DISPLAY_MODES = ("windowed", "fullscreen")
POSTINIT_CONNECT_WAIT_FRAMES = 30
POSTINIT_RECONNECT_WAIT_FRAMES = 30
MP_SERVER_CLIENT_GRACE_MSEC = 90000
REPORT_SCHEMA_VERSION = 5
GIT_PROVENANCE_POLICY = "current-openq4-head-and-dirty-state-v1"
BUDGET_DISPLAY_CONTRACT_ID = "bordered-window-1280x720-v1"
BUDGET_WIDTH = 1280
BUDGET_HEIGHT = 720
PBR_ACCEPTANCE_COMMON_CVARS = (
    ("r_materialoverride", PBR_FIXTURE_MATERIAL_NAME),
    ("r_pbrmaterials", "1"),
    ("r_renderermodernquality", "1"),
)
PBR_ACCEPTANCE_GL_CVARS = (
    *PBR_ACCEPTANCE_COMMON_CVARS,
    ("r_renderermodernvisible", "1"),
    ("r_rendererforwardplus", "1"),
)
PBR_PROTECTED_CVARS = frozenset(name for name, _ in PBR_ACCEPTANCE_GL_CVARS)
PBR_BOOL_CVARS = PBR_PROTECTED_CVARS - {"r_materialoverride"}
PBR_CVAR_MUTATOR_COMMANDS = frozenset(
    {
        "cycle",
        "reset",
        "set",
        "seta",
        "sets",
        "sett",
        "setu",
        "toggle",
        "togglecvar",
    }
)
PBR_INDIRECT_COMMANDS = frozenset(
    {
        "cvar_restart",
        "connect",
        "disconnect",
        "exec",
        "execmachinespec",
        "devmap",
        "map",
        "maprestart",
        "nextmap",
        "reconnect",
        "reloadmap",
        "reloadengine",
        "setmachinespec",
        "spawnserver",
        "vstr",
    }
)
PBR_EVIDENCE_OUTPUT_COMMANDS = frozenset(
    {"echo", "rendererbenchmarkcapture", "say", "sayteam"}
)
PBR_EVIDENCE_MARKERS = (
    "PBR material resources:",
    "Modern GL executor:",
    "Modern forward+:",
    "Modern visible frame:",
    "Vulkan: native PBR direct interactions active (",
)
BENCHMARK_EVIDENCE_MARKERS = (
    *PBR_EVIDENCE_MARKERS,
    TIMING_EVIDENCE_MARKER,
    "rendererBenchmark capture(",
    "Renderer benchmark:",
    "Frame pacing",
    "Selected renderer tier:",
    "MODE:",
    "Map:",
    "OPENQ4_BENCHMARK_VIEW_",
    "openQ4 map state:",
    "origin:",
)
VIEW_BEGIN = "OPENQ4_BENCHMARK_VIEW_BEGIN"
VIEW_END = "OPENQ4_BENCHMARK_VIEW_END"
POST_MAP_CVARS_BEGIN = "// OPENQ4_BENCHMARK_POST_MAP_CVARS_V1_BEGIN"
POST_MAP_CVARS_END = "// OPENQ4_BENCHMARK_POST_MAP_CVARS_V1_END"
EXEC_COMMANDS_BEGIN = "// OPENQ4_BENCHMARK_EXEC_COMMANDS_V1_BEGIN"
EXEC_COMMANDS_END = "// OPENQ4_BENCHMARK_EXEC_COMMANDS_V1_END"
# Each role already owns an isolated save path. Keep the engine-side log name
# deliberately short so the complete fs_savepath/baseoq4/logs path remains
# below the legacy Windows MAX_PATH boundary even for descriptive MP case IDs.
ROLE_LOG_NAME = "openq4_gameplay.log"
RUNTIME_DISPLAY_MODE_PATTERN = re.compile(
    r"^MODE:\s*([^,\r\n]+),\s*(\d+)\s+x\s+(\d+)\s+"
    r"(windowed|borderless|fullscreen)\b",
    re.IGNORECASE | re.MULTILINE,
)
ENGINE_MAP_PATTERN = re.compile(
    r"^Map:\s*([A-Za-z0-9_./\\-]+)\s*$", re.IGNORECASE | re.MULTILINE
)

REQUIRED_SCENES: dict[str, dict[str, Any]] = {
    "sp-storage1": {
        "mode": "SP",
        "map": "game/storage1",
        "purpose": "stock first-entry drop-pod sequence; scripted camera, not a static indoor gameplay reference",
        "path": "scripted-intro",
        "entityFilter": "first",
        "viewPolicy": "record-endpoints",
    },
    "sp-storage1-second": {
        "mode": "SP",
        "map": "game/storage1",
        "purpose": "stock second-entry gameplay after the starting lift settles; separate from first-entry timing",
        "path": "spawn-static",
        "entityFilter": "second",
        "viewPolicy": "static-endpoints",
        "minimumSettleFrames": 720,
        # Frames alone under-settle a fast build: the lift rises in game time,
        # and 720 frames at a 240 fps cap is only 3 s. Real time follows the
        # frame wait so the camera has docked before sampling on any build.
        "minimumSettleMsec": 6000,
    },
    "sp-airdefense1": {
        "mode": "SP",
        "map": "game/airdefense1",
        "purpose": "stock SP baseline, outdoor lighting, terrain decals, and BSE smoke",
        "path": "spawn-static",
    },
    "sp-airdefense2": {
        "mode": "SP",
        "map": "game/airdefense2",
        "purpose": "flashlight, projected shadows, animated characters, and dynamic shadow receivers",
        "path": "spawn-static",
    },
    "sp-storage2": {
        "mode": "SP",
        "map": "game/storage2",
        "purpose": "indoor materials, post-process coverage, and dense local lights",
        "path": "spawn-static",
    },
    "sp-medlabs": {
        "mode": "SP",
        "map": "game/medlabs",
        "purpose": "BSE-heavy SP scene and stock scripted effects coverage",
        "path": "spawn-static",
    },
    "sp-mcc-landing": {
        "mode": "SP",
        "map": "game/mcc_landing",
        "purpose": "subviews, remote cameras, cinematic handoff, and GUI interaction",
        "path": "spawn-static",
    },
    "mp-q4dm1-listen": {
        "mode": "MP",
        "map": "mp/q4dm1",
        "purpose": "listen server plus local loopback client renderer parity",
        "path": "spawn-static",
    },
    "mp-q4dm1-postinit-connect": {
        "mode": "MP",
        "map": "mp/q4dm1",
        "purpose": "delayed IPv4 loopback connect after initial SP module startup, reconnect, and second-map gameplay capture",
        "path": "postinit-connect",
    },
}

SHADOW_SCENES: dict[str, dict[str, Any]] = {
    "shadow-projected-airdefense2": {
        "mode": "SP",
        "map": "game/airdefense2",
        "purpose": "angled projected-light caster/receiver validation",
        "path": "spawn-static",
    },
    "shadow-point-storage2": {
        "mode": "SP",
        "map": "game/storage2",
        "purpose": "point-light face coverage and local-light receiver validation",
        "path": "spawn-static",
    },
    "shadow-csm-airdefense1": {
        "mode": "SP",
        "map": "game/airdefense1",
        "purpose": "CSM camera sweep readiness and outdoor directional coverage",
        "path": "spawn-static",
    },
    "shadow-cutout-storage2": {
        "mode": "SP",
        "map": "game/storage2",
        "purpose": "hashed-alpha cutout fence/grate caster validation at distance",
        "path": "spawn-static",
    },
    "shadow-character-airdefense2": {
        "mode": "SP",
        "map": "game/airdefense2",
        "purpose": "dynamic character shadow caster and receiver validation",
        "path": "spawn-static",
    },
    "shadow-translucent-medlabs": {
        "mode": "SP",
        "map": "game/medlabs",
        "purpose": "optional translucent moment caster coverage where the selected tier supports it",
        "path": "spawn-static",
    },
}

CAMPAIGN_TRANSITION_SCENES: dict[str, dict[str, Any]] = {
    "sp-campaign-mcc2-to-tram1": {
        "mode": "SP",
        "map": "game/mcc_2",
        "purpose": "scripted campaign transition chain from MCC 2 through Storage 1 first/second state handling into Tram 1",
        "path": "triggered-campaign-transition",
        "budgetMap": "game/tram1",
    },
}

LOAD_REGRESSION_SCENES: dict[str, dict[str, Any]] = {
    "mp-q4dm9-listen": {
        "mode": "MP",
        "map": "mp/q4dm9",
        "purpose": "listen-server load-time and Vulkan shadow-fallback regression coverage",
        "path": "spawn-static",
    },
}

WORLD_AMBIENT_SCENES: dict[str, dict[str, Any]] = {
    "sp-mv2-ambient": {
        "mode": "SP",
        "map": "maps/tools/mv2",
        "purpose": "controlled stock fixed-function world-ambient ownership and whole-view rollback evidence",
        "path": "spawn-static",
    },
}

INTERACTION_SCENES: dict[str, dict[str, Any]] = {
    "sp-mv2-interaction": {
        "mode": "SP",
        "map": "maps/tools/mv2",
        "purpose": "controlled stock caster/receiver crates, projected light, and side point light for fixed-classic interaction/shadow ownership evidence",
        "path": "spawn-static",
    },
}

FOG_BLEND_SCENES: dict[str, dict[str, Any]] = {
    "sp-mv2-fog-blend": {
        "mode": "SP",
        "map": "maps/tools/mv2",
        "purpose": "controlled stock fog and blend lights for atomic fixed-classic ownership evidence",
        "path": "spawn-static",
    },
}

DEFORM_SCENES: dict[str, dict[str, Any]] = {
    "sp-mv2-deform": {
        "mode": "SP",
        "map": "maps/tools/mv2",
        "purpose": "controlled stock material-move deformation ownership and published-geometry parity evidence",
        "path": "spawn-static",
    },
}

MILESTONE_D_SCENES: dict[str, dict[str, Any]] = {
    "sp-milestone-d-nested-dynamic": {
        "mode": "SP",
        "map": "maps/tools/milestone_d_nested_dynamic",
        "purpose": "temporary stock-derived mirror fixture with an authored post/video tail visible only in the captured child view",
        "path": "spawn-static",
    },
}

# Stock-map qualification candidates complement the tightly controlled
# tools-map case.  The ordinary shadow-regression scenes do not by themselves
# establish a fixed camera or feature-bearing interaction view, so every target
# remains fail-closed until its exact map records prove the advertised class or
# caster feature.  Release evidence must retain the final camera pose together
# with matching classic/shadows-off references.
INTERACTION_SHADOW_SCENES: dict[str, dict[str, Any]] = {
    "shadow-projected-airdefense2": {
        **SHADOW_SCENES["shadow-projected-airdefense2"],
        "purpose": "projected-map qualification candidate requiring a retained authored-projector camera",
        "interactionShadowTarget": "projected",
    },
    "shadow-point-airdefense2": {
        **SHADOW_SCENES["shadow-projected-airdefense2"],
        "purpose": "ordinary stock spawn candidate for point-cube interaction ownership",
        "interactionShadowTarget": "point",
    },
    "shadow-csm-airdefense1": {
        **SHADOW_SCENES["shadow-csm-airdefense1"],
        "purpose": "multi-cascade projected/parallel qualification candidate requiring a retained camera",
        "interactionShadowTarget": "csm",
    },
    "shadow-character-airdefense2": {
        **SHADOW_SCENES["shadow-character-airdefense2"],
        "purpose": "dynamic mapped-caster qualification candidate; the gate does not assume character eligibility",
        "interactionShadowTarget": "dynamic",
    },
    "shadow-cutout-storage2": {
        **SHADOW_SCENES["shadow-cutout-storage2"],
        "purpose": "perforated mapped-caster qualification candidate requiring a retained cutout view",
        "interactionShadowTarget": "perforated",
    },
    "shadow-hybrid-storage2": {
        **SHADOW_SCENES["shadow-point-storage2"],
        "purpose": "same-light mapped/stencil-supplement qualification candidate requiring a retained hybrid view",
        "interactionShadowTarget": "hybrid",
    },
    "shadow-translucent-medlabs": {
        **SHADOW_SCENES["shadow-translucent-medlabs"],
        "purpose": "translucent-moment atomic-fallback qualification candidate",
        "interactionShadowTarget": "fallback",
    },
}

CAMPAIGN_MCC2_TO_TRAM1_COMMANDS = (
    "openq4_assertMapState game/mcc_2",
    "trigger mcc2_endlevel",
    "wait 180",
    "openq4_assertMapState game/storage1 first",
    "trigger endLevel",
    "wait 180",
    "openq4_assertMapState game/storage2",
    "trigger target_endlevel_1",
    "wait 180",
    "openq4_assertMapState game/storage1 second",
    "trigger target_endlevel_2",
    "wait 180",
    "openq4_assertMapState game/tram1",
)

FULL_BUDGET_SCENES = {
    **REQUIRED_SCENES,
    **SHADOW_SCENES,
    **CAMPAIGN_TRANSITION_SCENES,
}
ALL_SCENES = {
    **FULL_BUDGET_SCENES,
    **WORLD_AMBIENT_SCENES,
    **INTERACTION_SCENES,
    **FOG_BLEND_SCENES,
    **DEFORM_SCENES,
    **MILESTONE_D_SCENES,
    **INTERACTION_SHADOW_SCENES,
    **LOAD_REGRESSION_SCENES,
}

SHADOW_PRESETS: dict[str, dict[str, str]] = {
    "default": {},
    "unshadowed": {
        "r_shadows": "0",
        "r_useShadowMap": "0",
    },
    "stencil": {
        "r_shadows": "1",
        "r_useShadowMap": "0",
    },
    "mapped": {
        "r_shadows": "1",
        "r_useShadowMap": "1",
        "r_shadowMapCSM": "0",
        "r_shadowMapHashedAlpha": "1",
        "r_shadowMapTranslucentMoments": "0",
    },
    "mixed": {
        "r_shadows": "1",
        "r_useShadowMap": "1",
        "r_shadowMapCSM": "0",
        "r_shadowMapPointLights": "0",
        "r_shadowMapHashedAlpha": "1",
        "r_shadowMapTranslucentMoments": "0",
    },
    "csm": {
        "r_shadows": "1",
        "r_useShadowMap": "1",
        "r_shadowMapCSM": "1",
        "r_shadowMapHashedAlpha": "1",
        "r_shadowMapTranslucentMoments": "0",
    },
    "translucent": {
        "r_shadows": "1",
        "r_useShadowMap": "1",
        "r_shadowMapCSM": "1",
        "r_shadowMapHashedAlpha": "1",
        "r_shadowMapTranslucentMoments": "1",
    },
    "map-budget-fallback": {
        "r_shadows": "1",
        "r_useShadowMap": "1",
        "r_shadowMapCSM": "0",
        "r_shadowMapHashedAlpha": "1",
        "r_shadowMapTranslucentMoments": "0",
        # The controlled scene contains multiple ownership passes. With no
        # resident reuse and one admitted update, both backends must reject
        # the whole shared view before its first interaction write.
        "r_shadowMapStaticCache": "0",
        "r_shadowMapMaxUpdatesPerView": "1",
    },
}

SHADOW_DEBUG_PRESET_MODES = (1, 2, 3, 4, 5, 6, 7, 12, 13, 14)

for debug_mode in SHADOW_DEBUG_PRESET_MODES:
    SHADOW_PRESETS[f"debug{debug_mode}"] = {
        "r_shadows": "1",
        "r_useShadowMap": "1",
        "r_shadowMapCSM": "1",
        "r_shadowMapHashedAlpha": "1",
        "r_shadowMapDebugOverlay": "1",
        "r_shadowMapDebugMode": str(debug_mode),
        "r_shadowMapTranslucentMoments": "0",
    }

PROFILE_DEFAULTS = {
    "smoke": {
        "cases": ("sp-storage1",),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
    },
    "required": {
        "cases": tuple(REQUIRED_SCENES.keys()),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
    },
    "mp-postinit-connect": {
        "cases": ("mp-q4dm1-postinit-connect",),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
    },
    "campaign-split-state-transition": {
        "cases": tuple(CAMPAIGN_TRANSITION_SCENES.keys()),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "execCommands": CAMPAIGN_MCC2_TO_TRAM1_COMMANDS,
    },
    "world-ambient": {
        "cases": tuple(WORLD_AMBIENT_SCENES.keys()),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "launchCvars": (
            ("ui_showGun", "0"),
            ("g_showHud", "0"),
            ("r_multiSamples", "0"),
        ),
        # The tools-map spawn looks into an all-caulk horizon.  After normal
        # spawn settles, lock the player in noclip and place the engine view
        # over the static stock floor for a repeatable authored-surface capture.
        "execCommands": (
            "noclip",
            "setviewpos 0 0 256 80 0 0",
        ),
        "cvars": (
            ("g_renderFastNoPost", "1"),
            ("g_renderFastNoPostDirect", "1"),
            ("r_postAA", "0"),
            ("r_singleLight", "2147483647"),
            ("r_skipSubviews", "1"),
            ("r_useLightGrid", "0"),
            ("r_skipPlayerVisibilityEffects", "1"),
            ("r_portalsDistanceCull", "0"),
            ("r_forceAmbient", "0"),
            ("r_celShading", "0"),
            ("r_celShadingWorld", "0"),
            ("r_showOverDraw", "0"),
            ("r_singleTriangle", "0"),
            ("r_skipAmbient", "0"),
            ("r_skipNewAmbient", "0"),
            ("r_skipDeforms", "0"),
            ("r_skipRender", "0"),
        ),
    },
    "deform": {
        "cases": tuple(DEFORM_SCENES.keys()),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "launchCvars": (
            # shaderDemos/move evaluates a time-based deform expression.  Run
            # exactly one game tic per rendered frame so separate classic and
            # shared captures reach the same deform value on every backend.
            ("com_fixedTic", "1"),
            ("g_stopTime", "1"),
            ("in_mouse", "0"),
            ("ui_showGun", "0"),
            ("g_showHud", "0"),
            ("r_multiSamples", "0"),
        ),
        "execCommands": (
            "g_stopTime 1",
            "noclip",
            "setviewpos 0 0 256 80 0 0",
        ),
        # Keep the stock material override in the ordinary profile-cvar stream.
        # User --set-cvar entries are appended afterwards and can still replace
        # it for targeted deform-kind or atomic-fallback captures.
        "cvars": (
            # Advance only the common post-map settle interval, then the first
            # exec command freezes that exact tic for sampling and screenshot.
            ("g_stopTime", "0"),
            ("r_rendererSharedWorldAmbient", "1"),
            ("r_rendererSharedDeform", "1"),
            ("r_materialOverride", "shaderDemos/move"),
            ("g_renderFastNoPost", "1"),
            ("g_renderFastNoPostDirect", "1"),
            ("r_postAA", "0"),
            ("r_singleLight", "2147483647"),
            ("r_skipSubviews", "1"),
            ("r_useLightGrid", "0"),
            ("r_skipPlayerVisibilityEffects", "1"),
            ("r_portalsDistanceCull", "0"),
            ("r_forceAmbient", "0"),
            ("r_celShading", "0"),
            ("r_celShadingWorld", "0"),
            ("r_showOverDraw", "0"),
            ("r_singleTriangle", "0"),
            ("r_skipAmbient", "0"),
            ("r_skipNewAmbient", "0"),
            ("r_skipDeforms", "0"),
            ("r_skipRender", "0"),
        ),
    },
    "milestone-d-nested-dynamic": {
        "cases": tuple(MILESTONE_D_SCENES.keys()),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "launchCvars": (
            ("com_fixedTic", "1"),
            ("g_stopTime", "1"),
            ("in_mouse", "0"),
            ("ui_showGun", "0"),
            ("g_showHud", "0"),
            ("r_multiSamples", "0"),
        ),
        "execCommands": (
            "g_stopTime 1",
            "noclip",
            "setviewpos 0 -384 96 0 90 0",
        ),
        "cvars": (
            # Let map startup advance through the ordinary settle interval,
            # then freeze the exact child-view video clock before sampling.
            ("g_stopTime", "0"),
            ("r_rendererSharedSubview", "1"),
            ("r_rendererSharedCinematicPost", "1"),
            ("g_renderFastNoPost", "1"),
            ("g_renderFastNoPostDirect", "1"),
            ("g_renderCasUpscale", "0"),
            ("r_postAA", "0"),
            ("r_screenFraction", "100"),
            ("r_bloom", "0"),
            ("r_motionBlur", "0"),
            ("r_ssao", "0"),
            ("r_hdrToneMap", "0"),
            ("r_hdrDebugView", "0"),
            ("r_skipSubviews", "0"),
            ("r_useLightGrid", "0"),
            ("r_skipPlayerVisibilityEffects", "1"),
            ("r_portalsDistanceCull", "0"),
            ("r_forceAmbient", "0"),
            ("r_celShading", "0"),
            ("r_celShadingWorld", "0"),
            ("r_showOverDraw", "0"),
            ("r_singleTriangle", "0"),
            ("r_skipAmbient", "0"),
            ("r_skipNewAmbient", "0"),
            ("r_skipDeforms", "0"),
            ("r_skipRender", "0"),
            ("r_skipRenderContext", "0"),
            ("r_skipDynamicTextures", "0"),
            ("r_skipCopyTexture", "0"),
            ("r_skipPostProcess", "0"),
        ),
    },
    "interaction": {
        "cases": tuple(INTERACTION_SCENES.keys()),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": (
            "unshadowed",
            "stencil",
            "mapped",
            "mixed",
            "map-budget-fallback",
        ),
        "launchCvars": (
            ("ui_showGun", "0"),
            ("g_showHud", "0"),
            ("r_multiSamples", "0"),
        ),
        "execCommands": (
            "noclip",
            "setviewpos 0 -192 96 20 90 0",
            "testModel models/mapobjects/strogg/crates/crate1_small.lwo",
            'spawn func_static model models/mapobjects/strogg/crates/crate1_medium.lwo origin "-90 -70 -5.7"',
            'testPointLight 300 origin "96 -128 0"',
            "testLight",
        ),
        "cvars": (
            ("r_rendererSharedWorldInteraction", "1"),
            ("g_renderFastNoPost", "1"),
            ("g_renderFastNoPostDirect", "1"),
            ("g_renderCasUpscale", "0"),
            ("r_postAA", "0"),
            ("r_screenFraction", "100"),
            ("r_bloom", "0"),
            ("r_motionBlur", "0"),
            ("r_ssao", "0"),
            ("r_hdrToneMap", "0"),
            ("r_hdrDebugView", "0"),
            ("r_skipSubviews", "1"),
            ("r_useLightGrid", "0"),
            ("r_skipPlayerVisibilityEffects", "1"),
            ("r_portalsDistanceCull", "0"),
            ("r_forceAmbient", "0"),
            ("r_celShading", "0"),
            ("r_celShadingWorld", "0"),
            ("r_showOverDraw", "0"),
            ("r_singleTriangle", "0"),
            ("r_skipAmbient", "1"),
            ("r_skipNewAmbient", "1"),
            ("r_skipDeforms", "0"),
            ("r_skipRender", "0"),
        ),
    },
    "fog-blend": {
        "cases": tuple(FOG_BLEND_SCENES.keys()),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "launchCvars": (
            ("ui_showGun", "0"),
            ("g_showHud", "0"),
            ("r_multiSamples", "0"),
        ),
        # Use only stock map/model/light declarations.  The two test lights
        # deliberately exercise both halves of the atomic fog/blend domain;
        # lights/fog_generic is a fogLight and lights/fog_ambient is a
        # deterministic blendLight in the shipped Quake 4 declarations.
        # Do not use lights/stream_fog here: its time-driven flicker tables
        # make independent shared-off/shared-on image captures non-comparable.
        "execCommands": (
            "noclip",
            "setviewpos 0 -192 96 20 90 0",
            "testModel models/mapobjects/strogg/crates/crate1_small.lwo",
            'testPointLight 256 texture lights/fog_generic origin "0 96 48" _color "0.18 0.26 0.34" shaderParm3 "384" noShadows "1"',
            'testPointLight 256 texture lights/fog_ambient origin "96 -96 48" _color "0.28 0.10 0.05" shaderParm3 "0.65" noShadows "1"',
        ),
        "cvars": (
            ("r_rendererSharedWorldFogBlend", "1"),
            ("g_renderFastNoPost", "1"),
            ("g_renderFastNoPostDirect", "1"),
            ("g_renderCasUpscale", "0"),
            ("r_postAA", "0"),
            ("r_screenFraction", "100"),
            ("r_bloom", "0"),
            ("r_motionBlur", "0"),
            ("r_ssao", "0"),
            ("r_hdrToneMap", "0"),
            ("r_hdrDebugView", "0"),
            ("r_skipSubviews", "1"),
            ("r_useLightGrid", "0"),
            ("r_skipPlayerVisibilityEffects", "1"),
            ("r_portalsDistanceCull", "0"),
            ("r_forceAmbient", "0"),
            ("r_celShading", "0"),
            ("r_celShadingWorld", "0"),
            ("r_showOverDraw", "0"),
            ("r_singleTriangle", "0"),
            ("r_skipAmbient", "1"),
            ("r_skipNewAmbient", "1"),
            ("r_skipFogLights", "0"),
            ("r_skipBlendLights", "0"),
            ("r_skipDeforms", "0"),
            ("r_skipRender", "0"),
        ),
    },
    "interaction-shadow-stock": {
        "cases": tuple(INTERACTION_SHADOW_SCENES.keys()),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("unshadowed", "mapped", "csm", "translucent"),
        "caseShadows": {
            "shadow-projected-airdefense2": ("unshadowed", "mapped"),
            "shadow-point-airdefense2": ("unshadowed", "mapped"),
            "shadow-csm-airdefense1": ("unshadowed", "csm"),
            "shadow-character-airdefense2": ("unshadowed", "csm"),
            "shadow-cutout-storage2": ("unshadowed", "csm"),
            "shadow-hybrid-storage2": ("unshadowed", "mapped"),
            "shadow-translucent-medlabs": ("translucent",),
        },
        "launchCvars": (
            ("g_stopTime", "1"),
            ("ui_showGun", "0"),
            ("r_multiSamples", "0"),
        ),
        # Start deterministic at tic zero, then advance the real SP scene
        # through the normal settle interval before freezing it again for
        # sampling.  Keeping tic zero frozen only captures dark/empty spawn
        # state; leaving the viewmodel visible turns the aggregate interaction
        # packet into an unsupported weapon-depth-hack pass.
        "execCommands": (
            "g_stopTime 1",
        ),
        "cvars": (
            ("r_rendererSharedWorldInteraction", "1"),
            ("g_stopTime", "0"),
            ("g_renderFastNoPost", "1"),
            ("g_renderFastNoPostDirect", "1"),
            ("g_renderCasUpscale", "0"),
            ("r_postAA", "0"),
            ("r_screenFraction", "100"),
            ("r_bloom", "0"),
            ("r_motionBlur", "0"),
            ("r_ssao", "0"),
            ("r_hdrToneMap", "0"),
            ("r_hdrDebugView", "0"),
            ("r_skipSubviews", "1"),
            ("r_useLightGrid", "0"),
            ("r_skipPlayerVisibilityEffects", "1"),
            ("r_portalsDistanceCull", "0"),
            ("r_celShading", "0"),
            ("r_celShadingWorld", "0"),
            ("r_showOverDraw", "0"),
            ("r_singleTriangle", "0"),
            ("r_skipDeforms", "0"),
            ("r_skipRender", "0"),
        ),
    },
    "tiers": {
        "cases": ("sp-airdefense1",),
        "tiers": SAFE_TIERS,
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
    },
    "presentation": {
        "cases": ("sp-airdefense1",),
        "tiers": ("auto",),
        "maxfps": PRESENTATION_MAXFPS,
        "swap": PRESENTATION_SWAP_INTERVALS,
        "display": DISPLAY_MODES,
        "shadows": ("default",),
    },
    "shadows": {
        "cases": tuple(SHADOW_SCENES.keys()),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("stencil", "mapped", "csm", "translucent", "debug1", "debug2", "debug3", "debug4", "debug5", "debug6", "debug7", "debug12", "debug13", "debug14"),
    },
    "shadow-regression": {
        "cases": (
            "shadow-projected-airdefense2",
            "shadow-point-storage2",
            "shadow-csm-airdefense1",
            "shadow-character-airdefense2",
            "shadow-cutout-storage2",
        ),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("csm",),
        "cvars": (
            ("r_shadowMapPointLights", "1"),
            ("r_shadowMapReport", "1"),
        ),
        # Freeze game time from tic 0 so animation/effect/weapon-raise state
        # is identical at capture. As a post-load cvar the freeze raced map
        # load timing and captures froze on different tics run to run (camera
        # micro-drift, weapon state deltas); as a launch cvar the capture
        # state is the spawn state, every run.
        "launchCvars": (
            ("g_stopTime", "1"),
        ),
    },
    "postaa-state-poison": {
        "cases": ("sp-airdefense1",),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "cvars": (
            ("r_postAA", "1"),
            ("r_postAAStatePoisonTest", "1"),
        ),
    },
    "postaa-high": {
        "cases": ("sp-airdefense1",),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "cvars": (
            ("r_postAA", "2"),
        ),
    },
    "postaa-ultra": {
        "cases": ("sp-airdefense1",),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "cvars": (
            ("r_postAA", "3"),
        ),
    },
    "postaa-color-prototype": {
        "cases": ("sp-airdefense1",),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "cvars": (
            ("r_postAA", "4"),
        ),
    },
    "full": {
        "cases": tuple(FULL_BUDGET_SCENES.keys()),
        "tiers": SAFE_TIERS,
        "maxfps": PRESENTATION_MAXFPS,
        "swap": PRESENTATION_SWAP_INTERVALS,
        "display": DISPLAY_MODES,
        "shadows": ("default", "stencil", "mapped", "csm", "translucent"),
    },
}

WARNING_PATTERNS = {
    "snPrintfOverflow": re.compile(r"idStr::snPrintf:\s*overflow", re.IGNORECASE),
    "idStrWarning": re.compile(r"WARNING:\s+idStr", re.IGNORECASE),
    "binaryImageCacheWrite": re.compile(
        r"WARNING:\s*(?:\^[0-9]\s*)*idBinaryImage:\s+Could not open (?:file|generated cache)\b",
        re.IGNORECASE,
    ),
    "expandedLoadscreenUpload": re.compile(
        r"WARNING:\s*(?:\^[0-9]\s*)*Could not (?:upload|restore) expanded loading background\b",
        re.IGNORECASE,
    ),
    "shaderCompile": re.compile(r"(shader compile|program link).*(failed|error)|failed to compile", re.IGNORECASE),
    "glError": re.compile(
        r"\bGL_(?:INVALID_[A-Z_]+|OUT_OF_MEMORY|STACK_(?:OVERFLOW|UNDERFLOW)|CONTEXT_LOST)\b"
        r"|OpenGL\s+error"
        r"|\bGL\s+debug\s+callback\b[^\r\n]{0,160}\btype\s*=\s*(?:error|undefined)\b"
        r"|\b(?:glGetError\s*(?:\(\s*\))?|GL_CheckErrors)\b[^\r\n]{0,48}"
        r"(?:0x(?!0+\b)[0-9A-F]+|[1-9][0-9]{2,})\b",
        re.IGNORECASE,
    ),
    "framebufferIncomplete": re.compile(
        r"\bGL_FRAMEBUFFER_(?:INCOMPLETE[A-Z0-9_]*|UNSUPPORTED|UNDEFINED)\b"
        r"|\b(?:framebuffer|FBO)\b[^\r\n]{0,64}\b(?:incomplete|unsupported)\b"
        r"|\b(?:incomplete|unsupported)\b[^\r\n]{0,32}\bframebuffer\b",
        re.IGNORECASE,
    ),
    "glDebugHighSeverity": re.compile(
        r"\bGL_DEBUG_SEVERITY_HIGH\b"
        r"|^(?=[^\r\n]*\b(?:GL|OpenGL)\b)"
        r"(?=[^\r\n]*\b(?:debug|callback)\b)"
        r"(?=[^\r\n]*(?:\bseverity\s*[:=]?\s*(?:high|0x9146|37190)\b|\bhigh[- ]severity\b|\[\s*high\s*\]))"
        r"[^\r\n]*$",
        re.IGNORECASE | re.MULTILINE,
    ),
    "vulkanValidation": re.compile(r"\bVulkan validation:", re.IGNORECASE),
    "vulkanVuid": re.compile(r"\bVUID-[A-Za-z0-9][A-Za-z0-9_.-]*\b"),
    "vulkanDrawFailure": re.compile(
        r"WARNING:(?:\^[0-9]|[ \t])*Vulkan\b[^\r\n]{0,192}\b(?:draw skipped|draw refused)\b",
        re.IGNORECASE,
    ),
    "vulkanCallFailed": re.compile(
        r"\bVulkan\b[^\r\n]{0,160}\bvk[A-Z][A-Za-z0-9_]*\b[^\r\n]{0,96}\bfailed\b",
        re.IGNORECASE,
    ),
    "fatal": re.compile(
        r"\bFatal Error\b|^[ \t]*(?:\*+[ \t]*)?FATAL[ \t]*:|(?:could not|unable to) initialize OpenGL",
        re.IGNORECASE | re.MULTILINE,
    ),
    "errorLine": re.compile(r"^[ \t]*(?:\*+[ \t]*)?ERROR(?:[ \t]*:|[ \t]*$)", re.MULTILINE),
    "mapStateMismatch": re.compile(r"ERROR:\s+openQ4 map state mismatch|openQ4 map state assertion", re.IGNORECASE),
}

MAX_FAILURE_DIAGNOSTICS = 32
MAX_FAILURE_DIAGNOSTIC_CHARS = 600


@dataclass(frozen=True)
class RunSpec:
    case_id: str
    mode: str
    map_name: str
    budget_map_name: str
    purpose: str
    path_name: str
    tier: str
    maxfps: str
    swap_interval: str
    display_mode: str
    shadow_preset: str
    renderer: str
    render_api: str
    interaction_expectation: str = "none"
    interaction_shadow_expectation: str = "none"
    fog_blend_expectation: str = "none"

    @property
    def scene_contract(self) -> dict[str, Any] | None:
        return scene_view_contract(self.case_id)

    @property
    def fullscreen(self) -> bool:
        return self.display_mode == "fullscreen"

    @property
    def expected_backend(self) -> str:
        return "vulkan" if self.render_api == "vk" else "opengl"

    @property
    def id(self) -> str:
        return self.id_for_shadow_preset(self.shadow_preset)

    def id_for_shadow_preset(self, shadow_preset: str) -> str:
        parts = [
            self.case_id,
            self.tier,
            f"fps{self.maxfps}",
            f"vsync{self.swap_interval}",
            self.display_mode,
            shadow_preset,
        ]
        parts.append(self.renderer)
        return sanitize_case_id("_".join(parts))


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def scene_view_contract(case_id: str) -> dict[str, Any] | None:
    """Only scenes with an explicit entry currently have a qualified view policy."""
    scene = ALL_SCENES.get(case_id, {})
    if not scene.get("entityFilter"):
        return None
    return {
        "contractId": "stock-entry-view-v2",
        "map": scene["map"],
        "entityFilter": scene["entityFilter"],
        "path": scene["path"],
        "viewPolicy": scene["viewPolicy"],
        "minimumSettleFrames": scene.get("minimumSettleFrames", 0),
        "minimumSettleMsec": scene.get("minimumSettleMsec", 0),
        # viewpos rounds each coordinate/angle to one decimal place. Allow
        # small view bob, but reject an unsettled lift or scripted flight.
        "maxPositionDelta": 1.0 if scene["viewPolicy"] == "static-endpoints" else None,
        "maxAngleDelta": 1.0 if scene["viewPolicy"] == "static-endpoints" else None,
        "expectedOrigin": [-2592.0, 2504.0, 4.2] if case_id == "sp-storage1-second" else None,
        "expectedAngles": [0.0, 90.0, 0.0] if case_id == "sp-storage1-second" else None,
    }


def scene_snapshot_commands(marker: str, map_name: str, entity_filter: str) -> list[str]:
    return [
        f"echo {marker}",
        f"openq4_assertMapState {map_name} {entity_filter}",
        "viewpos",
    ]


def host_arch() -> str:
    machine = platform.machine().lower()
    if machine in ("amd64", "x86_64"):
        return "x64"
    if machine in ("arm64", "aarch64"):
        return "arm64"
    if machine in ("x86", "i386", "i686"):
        return "x86"
    return machine


def find_client_executable(runtime_dir: Path) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    candidate_prefixes = ("openQ4-client", "openQ4-client")
    for prefix in candidate_prefixes:
        preferred = runtime_dir / f"{prefix}_{host_arch()}{suffix}"
        if preferred.exists():
            return preferred

    candidates: list[Path] = []
    seen: set[Path] = set()
    for prefix in candidate_prefixes:
        for candidate in sorted(runtime_dir.glob(f"{prefix}_*{suffix}")):
            if candidate not in seen:
                candidates.append(candidate)
                seen.add(candidate)

    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(f"openQ4 client executable not found under {runtime_dir}")


def is_link_or_junction(path: Path) -> bool:
    is_junction = getattr(path, "is_junction", None)
    if path.is_symlink() or bool(is_junction and is_junction()):
        return True
    try:
        attributes = getattr(path.lstat(), "st_file_attributes", 0)
        return bool(attributes & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0))
    except OSError:
        return False


def validate_runtime_dir(runtime_dir: Path, root: Path) -> Path:
    """Require canonical .install or a named ordinary package below .tmp."""
    root_absolute = root.absolute()
    candidate = runtime_dir.absolute()
    if is_link_or_junction(root_absolute):
        raise ValueError(f"source root must not be a link or junction: {root_absolute}")
    try:
        relative = candidate.relative_to(root_absolute)
    except ValueError as exc:
        raise ValueError(
            f"runtime directory must stay below source root {root_absolute}: {candidate}"
        ) from exc
    current = root_absolute
    for part in relative.parts:
        current /= part
        if current.exists() and is_link_or_junction(current):
            raise ValueError(
                f"runtime directory ancestry must not contain a link or junction: {current}"
            )
    resolved = candidate.resolve()
    if not resolved.is_dir():
        raise FileNotFoundError(f"runtime directory does not exist: {resolved}")
    canonical = (root / ".install").resolve()
    temporary_parent = (root / ".tmp" / "stock-runtime").resolve()
    if resolved != canonical:
        try:
            isolated_name = resolved.relative_to(temporary_parent)
        except ValueError as exc:
            raise ValueError(
                f"alternate runtime directory must stay below {temporary_parent}: {resolved}"
            ) from exc
        if not isolated_name.parts:
            raise ValueError(
                f"alternate runtime directory must be a named child below {temporary_parent}"
            )
    return resolved


def prepare_output_directory(output_dir: Path) -> None:
    if output_dir.exists():
        if not output_dir.is_dir():
            raise ValueError(f"benchmark output path is not a directory: {output_dir}")
        if any(output_dir.iterdir()):
            raise ValueError(
                f"benchmark output directory must be new or empty: {output_dir}"
            )
    else:
        output_dir.mkdir(parents=True)


def file_record(path: Path, root: Path) -> dict[str, Any]:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return {
        "path": path.relative_to(root).as_posix(),
        "size": path.stat().st_size,
        "sha256": digest.hexdigest(),
    }


def collect_runtime_files(runtime_dir: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for path in sorted(runtime_dir.rglob("*")):
        if is_link_or_junction(path):
            raise ValueError(f"runtime package must not contain links or junctions: {path}")
        if path.is_file():
            records.append(file_record(path, runtime_dir))
    if not records:
        raise ValueError(f"runtime package is empty: {runtime_dir}")
    return records


def git_state(root: Path) -> dict[str, Any]:
    def run(*arguments: str) -> str:
        completed = subprocess.run(
            ["git", *arguments], cwd=root, capture_output=True, text=True, check=False
        )
        return completed.stdout.strip() if completed.returncode == 0 else ""

    return {
        "policy": GIT_PROVENANCE_POLICY,
        "revision": run("rev-parse", "HEAD"),
        "dirty": bool(run("status", "--porcelain")),
    }


def path_hint(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return f"external:{path.name}"


def attach_result_artifacts(output_dir: Path, results: list[dict[str, Any]]) -> None:
    for result in results:
        for role in result.get("roles", []):
            artifacts: list[dict[str, Any]] = []
            for kind, field in (
                ("engineLog", "log"),
                ("processStdout", "stdout"),
                ("processStderr", "stderr"),
                ("screenshot", "screenshot"),
                ("benchmarkConfig", "autoexecCfg"),
            ):
                value = role.get(field)
                if not value:
                    continue
                path = Path(value)
                if path.is_file():
                    artifacts.append({"kind": kind, **file_record(path, output_dir)})
            role["artifacts"] = artifacts


def compare_file_records(
    expected: Any, actual: list[dict[str, Any]], description: str
) -> list[str]:
    if not isinstance(expected, list):
        return [f"recorded {description} inventory is missing or malformed"]
    expected_by_path = {
        item.get("path"): item
        for item in expected
        if isinstance(item, dict) and isinstance(item.get("path"), str)
    }
    actual_by_path = {item["path"]: item for item in actual}
    failures: list[str] = []
    if len(expected_by_path) != len(expected) or set(expected_by_path) != set(actual_by_path):
        failures.append(f"{description} path inventory differs")
    for path in sorted(set(expected_by_path) & set(actual_by_path)):
        if expected_by_path[path] != actual_by_path[path]:
            failures.append(f"{description} differs: {path}")
    return failures


def default_basepath() -> str:
    if os.name == "nt":
        return r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"
    return ""


def resolve_basepath(value: str) -> str:
    if not value:
        return ""
    path = Path(value)
    return str(path.resolve()) if path.exists() else ""


def sanitize_case_id(case_id: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", case_id)


def split_csv(value: str, defaults: tuple[str, ...]) -> tuple[str, ...]:
    if not value:
        return defaults
    return tuple(item.strip() for item in value.split(",") if item.strip())


def has_command_delimiter_or_control(value: str) -> bool:
    return ";" in value or any(not character.isprintable() for character in value)


def has_engine_token_expansion_or_comment(value: str) -> bool:
    return "$" in value or any(token in value for token in ("/*", "*/", "//"))


def contains_benchmark_evidence_text(value: str) -> bool:
    folded = value.casefold()
    return any(marker.casefold() in folded for marker in BENCHMARK_EVIDENCE_MARKERS)


def parse_extra_cvars(values: list[str]) -> tuple[tuple[str, str], ...]:
    parsed: list[tuple[str, str]] = []
    for raw in values:
        if has_command_delimiter_or_control(raw):
            raise ValueError(
                "extra cvar contains a command delimiter or control character"
            )
        item = raw.strip()
        if not item:
            continue
        if "=" in item:
            name, value = item.split("=", 1)
        else:
            parts = item.split(None, 1)
            if len(parts) != 2:
                raise ValueError(f"extra cvar '{raw}' must use name=value or 'name value'")
            name, value = parts
        name = name.strip()
        value = value.strip()
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
            raise ValueError(f"extra cvar name '{name}' is not a valid cvar identifier")
        if not value:
            raise ValueError(f"extra cvar '{name}' needs a value")
        if has_command_delimiter_or_control(value):
            raise ValueError(
                f"extra cvar '{name}' contains a command delimiter or control character"
            )
        if contains_benchmark_evidence_text(value):
            raise ValueError(
                f"extra cvar '{name}' may not contain benchmark evidence text"
            )
        if name.casefold() in PBR_PROTECTED_CVARS and (
            has_engine_token_expansion_or_comment(value)
            or any(character in value for character in ('"', "'", "\\"))
            or (name.casefold() in PBR_BOOL_CVARS and value.startswith("+"))
        ):
            raise ValueError(
                f"protected PBR cvar '{name}' contains ambiguous engine token syntax"
            )
        parsed.append((name, value))
    return tuple(parsed)


def effective_post_map_cvars(
    values: Iterable[tuple[str, str]],
) -> dict[str, str]:
    """Return the engine-style, last-write-wins post-map cvar selection."""
    effective: dict[str, str] = {}
    for name, value in values:
        effective[name.casefold()] = value
    return effective


def pbr_acceptance_fixture_cvars(render_api: str) -> tuple[tuple[str, str], ...]:
    if render_api == "gl":
        return PBR_ACCEPTANCE_GL_CVARS
    if render_api == "vk":
        return PBR_ACCEPTANCE_COMMON_CVARS
    raise ValueError(f"unsupported renderer API for PBR fixture acceptance: {render_api!r}")


def requires_pbr_fixture_acceptance(
    values: Iterable[tuple[str, str]], render_api: str,
) -> bool:
    """Recognize only the explicit controlled Milestone-F fixture for one API."""
    effective = effective_post_map_cvars(values)
    for name, expected in pbr_acceptance_fixture_cvars(render_api):
        value = effective.get(name)
        if value is None:
            return False
        if name in PBR_BOOL_CVARS:
            integer_prefix = re.match(r"^[+-]?\d+", value)
            if integer_prefix is None or int(integer_prefix.group(0)) == 0:
                return False
        elif value != expected:
            return False
    return True


def command_tokens(command: str) -> tuple[str, ...]:
    try:
        return tuple(shlex.split(command, posix=True))
    except ValueError as exc:
        raise ValueError(f"invalid --exec-command quoting: {command!r}") from exc


def exec_command_pbr_provenance_failure(command: str) -> str:
    """Explain command forms that can mutate protected fixture state."""
    tokens = command_tokens(command)
    if not tokens:
        return "empty command"
    first = tokens[0].lstrip("+/\\").casefold()
    if first in PBR_PROTECTED_CVARS:
        return f"directly mutates protected cvar {tokens[0]!r}"
    if first in PBR_CVAR_MUTATOR_COMMANDS and len(tokens) >= 2:
        target = tokens[1].casefold()
        if target in PBR_PROTECTED_CVARS:
            return f"mutates protected cvar {tokens[1]!r} through {tokens[0]!r}"
    if first in PBR_INDIRECT_COMMANDS:
        return f"uses indirect command source {tokens[0]!r}"
    if first in PBR_EVIDENCE_OUTPUT_COMMANDS:
        return f"uses evidence-output command {tokens[0]!r}"
    return ""


def parse_exec_commands(values: list[str]) -> tuple[str, ...]:
    commands: list[str] = []
    for raw in values:
        if has_command_delimiter_or_control(raw):
            raise ValueError(
                f"--exec-command contains a command delimiter or control character: {raw!r}"
            )
        if has_engine_token_expansion_or_comment(raw):
            raise ValueError(
                f"--exec-command contains engine token expansion or comment syntax: {raw!r}"
            )
        if contains_benchmark_evidence_text(raw):
            raise ValueError(
                f"--exec-command may not contain benchmark evidence text: {raw!r}"
            )
        command = raw.strip()
        if not command:
            raise ValueError("empty --exec-command value")
        provenance_failure = exec_command_pbr_provenance_failure(command)
        if provenance_failure:
            raise ValueError(
                f"--exec-command cannot preserve PBR fixture provenance: {provenance_failure}"
            )
        commands.append(command)
    return tuple(commands)


def marked_cfg_block(lines: list[str], begin: str, end: str) -> list[str]:
    if lines.count(begin) != 1 or lines.count(end) != 1:
        raise ValueError(f"benchmark config must contain exactly one {begin!r}/{end!r} block")
    start = lines.index(begin)
    finish = lines.index(end)
    if finish <= start:
        raise ValueError(f"benchmark config block {begin!r} is out of order")
    return lines[start + 1 : finish]


@dataclass(frozen=True)
class BenchmarkConfigEvidence:
    cvars: tuple[tuple[str, str], ...]
    commands: tuple[str, ...]
    capture_mode: str
    settle_frames: int
    settle_msec: int
    sample_kind: str
    sample_value: int
    role: str
    capture_index: int
    screenshot_request: str
    scene_map: str
    entity_filter: str


def parse_benchmark_config(text: str) -> BenchmarkConfigEvidence:
    """Validate the complete generated capture template and return its inputs."""
    lines = text.splitlines()
    prefix = [
        "r_rendererSharedGui 0",
        "r_rendererSharedInWorldGui 0",
        "r_rendererSharedCinematicPost 0",
        "r_rendererSharedSpecialFrame 0",
        "r_rendererSharedWorldAmbient 0",
        "r_rendererSharedWorldInteraction 0",
        "r_rendererSharedWorldFogBlend 0",
        "r_rendererSharedSubview 0",
        "r_rendererSharedDeform 0",
        "r_rendererModernVisible 0",
        "r_rendererModernVisibleDepth 0",
        "r_rendererModernOpaque 0",
        "r_rendererModernDeferred 0",
        "r_rendererForwardPlus 0",
        "r_rendererModernSubmit 0",
        "r_rendererGpuValidation 0",
        "r_rendererBindless 0",
        "r_rendererShaderReload 0",
        POST_MAP_CVARS_BEGIN,
    ]
    if lines[: len(prefix)] != prefix:
        raise ValueError("generated benchmark config prefix differs from the capture template")
    if lines.count(POST_MAP_CVARS_BEGIN) != 1 or lines.count(POST_MAP_CVARS_END) != 1:
        raise ValueError("generated benchmark config must contain one post-map CVar block")
    if lines.count(EXEC_COMMANDS_BEGIN) != 1 or lines.count(EXEC_COMMANDS_END) != 1:
        raise ValueError("generated benchmark config must contain one exec-command block")

    cursor = len(prefix)
    try:
        cvar_finish = lines.index(POST_MAP_CVARS_END, cursor)
    except ValueError as exc:
        raise ValueError("generated benchmark config post-map CVar block is unterminated") from exc
    cvar_lines = lines[cursor:cvar_finish]
    cvars: list[tuple[str, str]] = []
    for line in cvar_lines:
        if has_command_delimiter_or_control(line):
            raise ValueError("unsafe generated post-map cvar line")
        parts = line.strip().split(None, 2)
        if len(parts) != 3 or parts[0].casefold() != "set":
            raise ValueError(f"malformed generated post-map cvar line: {line!r}")
        _, name, value = parts
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
            raise ValueError(f"invalid generated post-map cvar name: {name!r}")
        if not value or has_command_delimiter_or_control(value):
            raise ValueError(f"unsafe generated post-map cvar value for {name!r}")
        if contains_benchmark_evidence_text(value):
            raise ValueError(
                f"generated post-map cvar {name!r} contains benchmark evidence text"
            )
        if name.casefold() in PBR_PROTECTED_CVARS and (
            has_engine_token_expansion_or_comment(value)
            or any(character in value for character in ('"', "'", "\\"))
            or (name.casefold() in PBR_BOOL_CVARS and value.startswith("+"))
        ):
            raise ValueError(f"ambiguous generated protected PBR cvar value for {name!r}")
        cvars.append((name, value))

    cursor = cvar_finish + 1
    if cursor >= len(lines):
        raise ValueError("generated benchmark config ends before the settle command")
    settle_match = re.fullmatch(r"wait ([1-9][0-9]*)", lines[cursor])
    if settle_match is None:
        raise ValueError("generated benchmark config has an invalid settle command")
    settle_frames = int(settle_match.group(1))
    cursor += 1
    settle_msec = 0
    settle_msec_match = re.fullmatch(r"waitMsec ([1-9][0-9]*)", lines[cursor] if cursor < len(lines) else "")
    if settle_msec_match is not None:
        settle_msec = int(settle_msec_match.group(1))
        cursor += 1
    fixed_before_exec = ["god", "notarget", EXEC_COMMANDS_BEGIN]
    if lines[cursor : cursor + len(fixed_before_exec)] != fixed_before_exec:
        raise ValueError("generated benchmark config pre-exec scaffold differs")
    cursor += len(fixed_before_exec)
    try:
        exec_finish = lines.index(EXEC_COMMANDS_END, cursor)
    except ValueError as exc:
        raise ValueError("generated benchmark config exec-command block is unterminated") from exc
    commands = parse_exec_commands(lines[cursor:exec_finish])
    cursor = exec_finish + 1
    scene_map = entity_filter = ""
    if lines[cursor : cursor + 1] == [f"echo {VIEW_BEGIN}"]:
        assertion = re.fullmatch(
            r"openq4_assertMapState ([A-Za-z0-9_./-]+) ([A-Za-z0-9_-]+)",
            lines[cursor + 1] if cursor + 1 < len(lines) else "",
        )
        if assertion is None:
            raise ValueError("generated benchmark config has an invalid scene assertion")
        scene_map, entity_filter = assertion.groups()
        start_commands = scene_snapshot_commands(VIEW_BEGIN, scene_map, entity_filter)
    else:
        # MP registers getviewpos; retain viewpos for existing SP evidence.
        start_commands = ["getviewpos"] if lines[cursor : cursor + 1] == ["getviewpos"] else ["viewpos"]
    if lines[cursor : cursor + len(start_commands)] != start_commands:
        raise ValueError("generated benchmark config is missing the post-command viewpos")
    cursor += len(start_commands)

    display_lines = [
        f"{name} {value}" for name, value in budget_display_contract()["cvars"].items()
    ]
    if lines[cursor : cursor + len(display_lines)] == display_lines:
        capture_mode = "budget"
        cursor += len(display_lines)
    else:
        capture_mode = "functional-replay"
    if cursor >= len(lines) or lines[cursor] != "framePacingReset":
        raise ValueError("generated benchmark config sampling scaffold differs")
    cursor += 1

    if capture_mode == "budget":
        sampling_prefix = ["r_rendererMetrics 1", "r_rendererGpuTimers 1"]
    else:
        sampling_prefix = ["r_rendererMetrics 0", "r_rendererGpuTimers 0"]
    if lines[cursor : cursor + len(sampling_prefix)] != sampling_prefix:
        raise ValueError(
            f"generated benchmark config does not prove {capture_mode} capture settings"
        )
    cursor += len(sampling_prefix)
    if cursor >= len(lines):
        raise ValueError("generated benchmark config is missing its sample wait")
    sample_match = re.fullmatch(r"(wait|waitMsec) ([1-9][0-9]*)", lines[cursor])
    if sample_match is None:
        raise ValueError("generated benchmark config has an invalid sample wait")
    sample_kind = sample_match.group(1)
    sample_value = int(sample_match.group(2))
    cursor += 1
    if capture_mode == "budget":
        budget_tail = ["rendererBenchmarkCapture", "r_rendererMetrics 0"]
        if lines[cursor : cursor + len(budget_tail)] != budget_tail:
            raise ValueError("generated budget config is missing its benchmark capture")
        cursor += len(budget_tail)

    fixed_tail = (
        scene_snapshot_commands(VIEW_END, scene_map, entity_filter) if scene_map else []
    ) + ["framePacingSnapshot", "gfxInfo"]
    if lines[cursor : cursor + len(fixed_tail)] != fixed_tail:
        raise ValueError("generated benchmark config evidence tail differs")
    cursor += len(fixed_tail)
    if cursor >= len(lines):
        raise ValueError("generated benchmark config is missing its screenshot command")
    screenshot_match = re.fullmatch(
        r'screenshot "(screenshots/renderer-bench/(sp|server|client)_([0-9]+)\.tga)"',
        lines[cursor],
    )
    if screenshot_match is None:
        raise ValueError("generated benchmark config screenshot request differs")
    screenshot_request, role, capture_index_text = screenshot_match.groups()
    cursor += 1
    if lines[cursor:] != ["wait 5", "quit"]:
        raise ValueError("generated benchmark config final commands differ")

    return BenchmarkConfigEvidence(
        cvars=tuple(cvars),
        commands=commands,
        capture_mode=capture_mode,
        settle_frames=settle_frames,
        settle_msec=settle_msec,
        sample_kind=sample_kind,
        sample_value=sample_value,
        role=role,
        capture_index=int(capture_index_text),
        screenshot_request=screenshot_request,
        scene_map=scene_map,
        entity_filter=entity_filter,
    )


def benchmark_cfg_provenance(
    text: str,
) -> tuple[tuple[tuple[str, str], ...], tuple[str, ...]]:
    """Extract provenance after validating every line of the generated config."""
    evidence = parse_benchmark_config(text)
    return evidence.cvars, evidence.commands


def append_set(args: list[str], name: str, value: Any) -> None:
    args += ["+set", name, str(value)]


def append_command(args: list[str], name: str, *values: Any) -> None:
    args.append("+" + name)
    args.extend(str(value) for value in values)


def display_launch_contract(spec: RunSpec, width: int, height: int) -> dict[str, Any]:
    """Return the exact, reportable display contract applied before startup."""
    promotable = (
        not spec.fullscreen and (width, height) == (BUDGET_WIDTH, BUDGET_HEIGHT)
    )
    return {
        "contractId": (
            BUDGET_DISPLAY_CONTRACT_ID
            if promotable
            else "non-promotable-diagnostic-display-v1"
        ),
        "width": width,
        "height": height,
        "cvars": {
            "r_fullscreen": "1" if spec.fullscreen else "0",
            "r_borderless": "0",
            "r_borderlessDefaultMigrated": "1",
            "r_fullscreenDesktop": "0",
            "r_windowWidth": str(width),
            "r_windowHeight": str(height),
            "r_mode": "-1",
            "r_customWidth": str(width),
            "r_customHeight": str(height),
        },
    }


def budget_display_contract() -> dict[str, Any]:
    spec = RunSpec(
        case_id="budget-display-contract",
        mode="SP",
        map_name="game/storage1",
        budget_map_name="game/storage1",
        purpose="budget display contract",
        path_name="spawn-static",
        tier="auto",
        maxfps="240",
        swap_interval="0",
        display_mode="windowed",
        shadow_preset="default",
        renderer="best",
        render_api="gl",
    )
    return display_launch_contract(spec, BUDGET_WIDTH, BUDGET_HEIGHT)


def common_args(
    root: Path,
    runtime_dir: Path,
    savepath: Path,
    log_name: str,
    basepath: str,
    spec: RunSpec,
    width: int,
    height: int,
    benchmark_preset: str,
    modern_executor: bool,
    show_fps_overlay: bool,
    launch_cvars: tuple[tuple[str, str], ...] = (),
    autoexec_cfg: str | None = None,
    autoexec_delay_ms: int = 1000,
) -> list[str]:
    args: list[str] = []
    multiple_instance_cvar = "win_allowMultipleInstances" if os.name == "nt" else "sys_allowMultipleInstances"
    append_set(args, multiple_instance_cvar, "1")
    append_set(args, "logFile", "2")
    append_set(args, "logFileName", f"logs/{log_name}")
    append_set(args, "developer", "1")
    append_set(args, "r_ignoreGLErrors", "0")
    append_set(args, "r_swapInterval", spec.swap_interval)
    append_set(args, "com_maxfps", spec.maxfps)
    append_set(args, "com_showFPS", "1" if show_fps_overlay else "0")
    append_set(args, "com_skipLoadingContinue", "1")
    append_set(args, "com_loadingContinueAutoAdvance", "1")
    append_set(args, "com_levelLoadModernization", "0")
    append_set(args, "g_autoSkipCinematics", "1")
    append_set(args, "g_autoScreenshot", "0")
    if autoexec_cfg:
        append_set(args, "g_autoExecAfterMapLoad", autoexec_cfg)
        append_set(args, "g_autoExecAfterMapLoadDelayMs", max(0, autoexec_delay_ms))
    append_set(args, "r_glTier", spec.tier)
    append_set(args, "r_renderer", spec.renderer)
    append_set(args, "r_rendererMetrics", "0")
    append_set(args, "r_rendererGpuTimers", "0")
    append_set(args, "r_rendererModernExecutor", "1" if modern_executor and spec.tier != "legacy" else "0")
    append_set(args, "r_rendererModernAutoPromote", "0")
    append_set(args, "r_rendererSharedGui", "0")
    append_set(args, "r_rendererSharedInWorldGui", "0")
    append_set(args, "r_rendererSharedCinematicPost", "0")
    append_set(args, "r_rendererSharedSpecialFrame", "0")
    append_set(args, "r_rendererSharedWorldAmbient", "0")
    append_set(args, "r_rendererSharedWorldInteraction", "0")
    append_set(args, "r_rendererSharedWorldFogBlend", "0")
    append_set(args, "r_rendererSharedSubview", "0")
    append_set(args, "r_rendererSharedDeform", "0")
    append_set(args, "r_rendererBenchmarkPreset", benchmark_preset)
    append_set(args, "fs_savepath", str(savepath))
    # Keep generated cache/config output in the isolated evidence root.  The
    # package itself is mounted by the locked fs_cdpath derived from cwd.
    append_set(args, "fs_devpath", str(savepath))
    append_set(args, "fs_game", "baseoq4")
    if basepath:
        append_set(args, "fs_basepath", basepath)

    for name, value in launch_cvars:
        append_set(args, name, value)

    # These startup-sensitive values are deliberately appended after optional
    # launch CVars.  Archived configs and ad-hoc A/B knobs therefore cannot
    # change the framebuffer size, presentation mode, or selected backend used
    # by replay-verifiable budget evidence.
    for name, value in display_launch_contract(spec, width, height)["cvars"].items():
        append_set(args, name, value)
    append_set(args, "r_renderApi", "vulkan" if spec.render_api == "vk" else "gl")

    for name, value in SHADOW_PRESETS[spec.shadow_preset].items():
        append_set(args, name, value)

    return args


def build_scripted_capture_lines(
    spec: RunSpec,
    role: str,
    run_id: str,
    settle_frames: int,
    sample_frames: int,
    sample_msec: int,
    extra_cvars: tuple[tuple[str, str], ...] = (),
    exec_commands: tuple[str, ...] = (),
    gpu_timers: bool = False,
    renderer_metrics: bool = True,
    capture_index: int = 0,
) -> tuple[list[str], str]:
    shot_name = f"screenshots/renderer-bench/{role}_{capture_index}.tga"
    scene_contract = spec.scene_contract
    if scene_contract:
        settle_frames = max(settle_frames, scene_contract["minimumSettleFrames"])
    lines: list[str] = [
        "r_rendererSharedGui 0",
        "r_rendererSharedInWorldGui 0",
        "r_rendererSharedCinematicPost 0",
        "r_rendererSharedSpecialFrame 0",
        "r_rendererSharedWorldAmbient 0",
        "r_rendererSharedWorldInteraction 0",
        "r_rendererSharedWorldFogBlend 0",
        "r_rendererSharedSubview 0",
        "r_rendererSharedDeform 0",
        "r_rendererModernVisible 0",
        "r_rendererModernVisibleDepth 0",
        "r_rendererModernOpaque 0",
        "r_rendererModernDeferred 0",
        "r_rendererForwardPlus 0",
        "r_rendererModernSubmit 0",
        "r_rendererGpuValidation 0",
        "r_rendererBindless 0",
        "r_rendererShaderReload 0",
        POST_MAP_CVARS_BEGIN,
    ]
    for name, value in extra_cvars:
        lines.append(f"set {name} {value}")
    lines += [POST_MAP_CVARS_END, f"wait {max(1, settle_frames)}"]
    if scene_contract and scene_contract.get("minimumSettleMsec", 0) > 0:
        lines.append(f"waitMsec {scene_contract['minimumSettleMsec']}")
    lines += [
        "god",
        "notarget",
        EXEC_COMMANDS_BEGIN,
    ]
    lines.extend(exec_commands)
    lines.append(EXEC_COMMANDS_END)
    # Record the pose that is actually sampled.  In particular, profile scene
    # commands may move the player after the initial map settle; getviewpos
    # before those commands described the wrong camera and omitted pitch/roll.
    lines.extend(
        scene_snapshot_commands(VIEW_BEGIN, scene_contract["map"], scene_contract["entityFilter"])
        if scene_contract else ["getviewpos" if spec.mode == "MP" else "viewpos"]
    )
    if renderer_metrics:
        # A client can reload the game module while connecting and re-exec an
        # archived config after the launch arguments were applied. Reassert
        # the promotion display contract immediately before sampling; actual
        # drawable dimensions are still verified independently from gfxInfo
        # and the engine-written TGA.
        for name, value in budget_display_contract()["cvars"].items():
            lines.append(f"{name} {value}")
    lines.append("framePacingReset")
    sample_wait = f"waitMsec {max(1, sample_msec)}" if sample_msec > 0 else f"wait {max(1, sample_frames)}"
    if renderer_metrics:
        lines += [
            "r_rendererMetrics 1",
            f"r_rendererGpuTimers {1 if gpu_timers else 0}",
            sample_wait,
            "rendererBenchmarkCapture",
            "r_rendererMetrics 0",
        ]
    else:
        lines += [
            "r_rendererMetrics 0",
            "r_rendererGpuTimers 0",
            sample_wait,
        ]
    if scene_contract:
        lines.extend(scene_snapshot_commands(
            VIEW_END, scene_contract["map"], scene_contract["entityFilter"]
        ))
    lines += [
        "framePacingSnapshot",
        "gfxInfo",
        f'screenshot "{shot_name}"',
        "wait 5",
        "quit",
    ]
    return lines, shot_name


def write_autoexec_cfg(
    savepath: Path,
    spec: RunSpec,
    role: str,
    run_id: str,
    settle_frames: int,
    sample_frames: int,
    sample_msec: int,
    extra_cvars: tuple[tuple[str, str], ...] = (),
    exec_commands: tuple[str, ...] = (),
    gpu_timers: bool = False,
    renderer_metrics: bool = True,
    capture_index: int = 0,
) -> tuple[str, str]:
    lines, shot_name = build_scripted_capture_lines(
        spec,
        role,
        run_id,
        settle_frames,
        sample_frames,
        sample_msec,
        extra_cvars,
        exec_commands,
        gpu_timers,
        renderer_metrics,
        capture_index,
    )
    cfg_rel = f"renderer-bench/{role}_{capture_index}.cfg"
    payload = "\n".join(lines) + "\n"
    screenshot_rel = Path(shot_name.replace("/", os.sep))
    for game_dir in ("baseoq4", "q4base"):
        cfg_path = savepath / game_dir / Path(cfg_rel)
        cfg_path.parent.mkdir(parents=True, exist_ok=True)
        cfg_path.write_text(payload, encoding="utf-8")
        screenshot_path = savepath / game_dir / screenshot_rel
        screenshot_path.parent.mkdir(parents=True, exist_ok=True)
    return cfg_rel, shot_name


def write_postinit_connect_cfg(savepath: Path, port: int) -> str:
    """Queue loopback connect after the client has finished its initial SP startup."""
    cfg_rel = "renderer-bench/postinit_connect.cfg"
    payload = f"wait {POSTINIT_CONNECT_WAIT_FRAMES}\nconnect 127.0.0.1:{port}\n"
    for game_dir in ("baseoq4", "q4base"):
        cfg_path = savepath / game_dir / Path(cfg_rel)
        cfg_path.parent.mkdir(parents=True, exist_ok=True)
        cfg_path.write_text(payload, encoding="utf-8")
    return cfg_rel


def write_postinit_reconnect_cfg(
    savepath: Path,
    capture_cfg: str,
    autoexec_delay_ms: int,
) -> str:
    """Reconnect from active MP play, then arm capture for the second map."""
    cfg_rel = "renderer-bench/postinit_reconnect.cfg"
    payload = (
        f"wait {POSTINIT_RECONNECT_WAIT_FRAMES}\n"
        f'set g_autoExecAfterMapLoad "{capture_cfg}"\n'
        f"set g_autoExecAfterMapLoadDelayMs {max(0, autoexec_delay_ms)}\n"
        "reconnect\n"
    )
    for game_dir in ("baseoq4", "q4base"):
        cfg_path = savepath / game_dir / Path(cfg_rel)
        cfg_path.parent.mkdir(parents=True, exist_ok=True)
        cfg_path.write_text(payload, encoding="utf-8")
    return cfg_rel


def find_log(savepath: Path, log_name: str) -> Path | None:
    candidates = [
        savepath / "baseoq4" / "logs" / log_name,
        savepath / "q4base" / "logs" / log_name,
        savepath / "logs" / log_name,
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def find_screenshot(savepath: Path, relative_name: str) -> Path | None:
    rel = Path(relative_name.replace("/", os.sep))
    for game_dir in ("baseoq4", "q4base"):
        candidate = savepath / game_dir / rel
        if candidate.exists():
            return candidate
    candidate = savepath / rel
    if candidate.exists():
        return candidate
    return None


def read_text(path: Path | None) -> str:
    if path is None or not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def warning_counts(text: str) -> dict[str, int]:
    return {name: len(pattern.findall(text)) for name, pattern in WARNING_PATTERNS.items()}


def collect_failure_diagnostics(
    sources: tuple[tuple[str, str], ...],
) -> tuple[list[dict[str, Any]], int]:
    diagnostics: list[dict[str, Any]] = []
    omitted = 0
    for source_name, source_text in sources:
        for line_number, raw_line in enumerate(source_text.splitlines(), start=1):
            signatures = [name for name, pattern in WARNING_PATTERNS.items() if pattern.search(raw_line)]
            if not signatures:
                continue
            line = raw_line.strip()
            if len(line) > MAX_FAILURE_DIAGNOSTIC_CHARS:
                line = line[: MAX_FAILURE_DIAGNOSTIC_CHARS - 3] + "..."
            if len(diagnostics) < MAX_FAILURE_DIAGNOSTICS:
                diagnostics.append(
                    {
                        "source": source_name,
                        "lineNumber": line_number,
                        "signatures": signatures,
                        "text": line,
                    }
                )
            else:
                omitted += 1
    return diagnostics, omitted


def format_failure_diagnostic(diagnostic: dict[str, Any]) -> str:
    signatures = ",".join(diagnostic["signatures"])
    return (
        f"{diagnostic['source']}:{diagnostic['lineNumber']} "
        f"[{signatures}] {diagnostic['text']}"
    )


def extract_last_line(
    text: str, token: str, required_tokens: tuple[str, ...] = ()
) -> str:
    lines = [
        line.strip()
        for line in text.splitlines()
        if token in line and all(required in line for required in required_tokens)
    ]
    return lines[-1] if lines else ""


def parse_frame_pacing(line: str) -> dict[str, str]:
    """Parse the live pacing diagnostics; present is a smoothed recent average."""
    if not line:
        return {}
    match = re.search(
        r"samples=(\d+).*?present=([0-9.]+) ms \(([0-9.]+) Hz\)"
        r"(?:, p50=([0-9.]+) ms, p95=([0-9.]+) ms, p99=([0-9.]+) ms, max=([0-9.]+) ms)?",
        line,
    )
    if not match:
        return {}
    samples, present_ms, present_hz, p50_ms, p95_ms, p99_ms, max_ms = match.groups()
    result = {
        "pacingSamples": samples,
        "pacingPresentMs": present_ms,
        "pacingHz": present_hz,
    }
    if p50_ms is not None:
        result.update(
            {
                "pacingP50Ms": p50_ms,
                "pacingP95Ms": p95_ms,
                "pacingP99Ms": p99_ms,
                "pacingMaxMs": max_ms,
            }
        )
    return result


def parse_frame_window(line: str) -> dict[str, str]:
    """Parse every measured interval since reset, including long frames."""
    match = re.search(r"Frame timing window .*?samples=(\d+), span_ms=(\d+), mean_ms=([0-9.]+)", line)
    if not match:
        return {}
    samples, span_ms, mean_ms = match.groups()
    result = {"pacingWindowSamples": samples, "pacingWindowSpanMs": span_ms, "pacingWindowMeanMs": mean_ms}
    if float(mean_ms) > 0 and int(samples) > 0:
        result["pacingWindowHz"] = f"{1000.0 / float(mean_ms):.6f}"
    return result


def extract_summary(text: str) -> dict[str, str]:
    interaction_summary_offset = text.rfind("Renderer shared interaction:")
    interaction_tail = (
        text[interaction_summary_offset:] if interaction_summary_offset >= 0 else ""
    )
    fog_blend_summary_offset = text.rfind("classicFogBlendDomain requested=")
    fog_blend_tail = (
        text[fog_blend_summary_offset:] if fog_blend_summary_offset >= 0 else ""
    )
    summary: dict[str, str] = {
        "benchmarkCapture": extract_last_line(text, "rendererBenchmark capture("),
        "benchmarkInfo": extract_last_line(text, "Renderer benchmark:"),
        "framePacing": extract_last_line(text, "Frame pacing"),
        "framePacingWindow": extract_last_line(text, "Frame timing window"),
        "selectedTier": extract_last_line(text, "Selected renderer tier:"),
        "tierContract": extract_last_line(text, "Renderer tier contract:"),
        "pbrMaterialResources": extract_last_line(
            text, "PBR material resources:"
        ),
        "modernGLExecutor": extract_last_line(
            text,
            "Modern GL executor:",
            ("drawPlan=", "planFallback=", "pbrOwners=", "pbrConsumed="),
        ),
        "modernForwardPlus": extract_last_line(text, "Modern forward+:"),
        "modernVisibleFrame": extract_last_line(text, "Modern visible frame:"),
        "vulkanPackedPBR": extract_last_line(
            text, "Vulkan: native PBR direct interactions active ("
        ),
        "sharedInteraction": extract_last_line(text, "Renderer shared interaction:"),
        "sharedInteractionView": extract_last_line(
            text, "Renderer shared interaction view["
        ),
        # Keep only the per-map records emitted by the latest gfxInfo block.
        # Earlier samples may contain a different frame/generation and must not
        # be reconciled with the final aggregate counters.
        "sharedInteractionMaps": "\n".join(
            line.strip()
            for line in interaction_tail.splitlines()
            if "Renderer shared interaction map view[" in line
        ),
        "sharedFogBlend": extract_last_line(
            text, "classicFogBlendDomain requested="
        ),
        # Keep backend records paired with the latest aggregate gfxInfo line.
        # Repeated samples can otherwise mix generations and make a backend
        # that was inactive in the final frame look owned.
        "sharedFogBlendBackends": "\n".join(
            line.strip()
            for line in fog_blend_tail.splitlines()
            if "classicFogBlendDomain backend=" in line
        ),
        "sharedFogBlendViews": "\n".join(
            line.strip()
            for line in fog_blend_tail.splitlines()
            if "classicFogBlendDomain view[" in line
        ),
    }
    matches = re.findall(
        r"rendererBenchmark capture\(.*?samples=(\d+).*?p50=(\d+).*?p95=(\d+).*?p99=(\d+)"
        r".*?\b(?:thresholdPass|pass)\s*=\s*(\d+)\b",
        text,
        re.IGNORECASE,
    )
    if matches:
        samples, p50, p95, p99, threshold_pass = matches[-1]
        summary.update(
            {
                "samples": samples,
                "p50": p50,
                "p95": p95,
                "p99": p99,
                "thresholdPass": threshold_pass,
            }
        )
    if "thresholdPass" not in summary:
        for benchmark_line in (summary["benchmarkCapture"], summary["benchmarkInfo"]):
            match = re.search(r"\b(?:thresholdPass|pass)\s*=\s*(\d+)\b", benchmark_line, re.IGNORECASE)
            if match:
                summary["thresholdPass"] = match.group(1)
                break
    summary.update(parse_frame_pacing(summary["framePacing"]))
    summary.update(parse_frame_window(summary["framePacingWindow"]))
    if "pacingWindowHz" in summary:
        # Match the average to the same complete sample window as p95/max.
        # The live HUD's last 120-frame moving average can hide a slow turn.
        summary["pacingRecentHz"] = summary.get("pacingHz", "")
        summary["pacingHz"] = summary["pacingWindowHz"]
        summary["pacingPresentMs"] = summary["pacingWindowMeanMs"]
    return summary


PBR_RESOURCE_TELEMETRY_PATTERN = re.compile(
    r"PBR material resources: "
    r"records=-?\d+ resourceReady=-?\d+ modernReady=-?\d+ "
    r"packed=-?\d+ separate=-?\d+ fallback=-?\d+"
    r"(?: disabled=-?\d+ workflow=-?\d+ class=-?\d+ albedo=-?\d+ "
    r"normalFormat=-?\d+ conflict=-?\d+ layout=-?\d+ image=-?\d+ "
    r"classic=-?\d+ units=-?\d+ shader=-?\d+ authored=-?\d+ "
    r"explicitGenerated=-?\d+ generated=-?\d+ approximate=-?\d+ "
    r"missingFallback=-?\d+ mapsMissing=-?\d+/-?\d+/-?\d+)?"
)
MODERN_GL_EXECUTOR_TELEMETRY_PATTERN = re.compile(
    r"Modern GL executor: (?:available|unavailable), "
    r"(?=[^\r\n]*\bdrawPlan=-?\d+\b)"
    r"(?=[^\r\n]*\bplanDraws=-?\d+\b)"
    r"(?=[^\r\n]*\bplanFallback=-?\d+\b)"
    r"(?=[^\r\n]*\bpbrOwners=-?\d+\b)"
    r"(?=[^\r\n]*\bpbrConsumed=-?\d+\b)"
    r"[A-Za-z0-9_(),='./ -]+"
)
MODERN_FORWARD_PLUS_TELEMETRY_PATTERN = re.compile(
    r"Modern forward\+: cvar=-?\d+, "
    r"(?=[^\r\n]*\breq=-?\d+\b)"
    r"(?=[^\r\n]*\bexec=-?\d+\b)"
    r"(?=[^\r\n]*\bresources=-?\d+\b)"
    r"(?=[^\r\n]*\bsceneColor=-?\d+\b)"
    r"(?=[^\r\n]*\bsceneDepth=-?\d+\b)"
    r"(?=[^\r\n]*\bprogram=-?\d+\b)"
    r"(?=[^\r\n]*\bcluster=-?\d+\b)"
    r"(?=[^\r\n]*\bdraws=-?\d+\b)"
    r"(?=[^\r\n]*\bfallback=-?\d+\b)"
    r"[A-Za-z0-9_(),='./ -]+"
)
MODERN_VISIBLE_TELEMETRY_PATTERN = re.compile(
    r"Modern visible frame: cvar=-?\d+, "
    r"(?=[^\r\n]*\breq=-?\d+\b)"
    r"(?=[^\r\n]*\bexec=-?\d+\b)"
    r"(?=[^\r\n]*\bresources=-?\d+\b)"
    r"(?=[^\r\n]*\bprogram=-?\d+\b)"
    r"(?=[^\r\n]*\bsource=-?\d+\b)"
    r"(?=[^\r\n]*\bhybrid=-?\d+\b)"
    r"(?=[^\r\n]*\bbackBuffer=-?\d+\b)"
    r"(?=[^\r\n]*\bcomposed=-?\d+\b)"
    r"[A-Za-z0-9_(),='./ -]+"
)
PBR_TELEMETRY_PATTERNS = {
    "pbrMaterialResources": PBR_RESOURCE_TELEMETRY_PATTERN,
    "modernGLExecutor": MODERN_GL_EXECUTOR_TELEMETRY_PATTERN,
    "modernForwardPlus": MODERN_FORWARD_PLUS_TELEMETRY_PATTERN,
    "modernVisibleFrame": MODERN_VISIBLE_TELEMETRY_PATTERN,
}


def normalize_engine_map_name(value: str) -> str:
    normalized = value.strip().replace("\\", "/").casefold().removesuffix(".map")
    while "//" in normalized:
        normalized = normalized.replace("//", "/")
    if not normalized.startswith("maps/"):
        normalized = "maps/" + normalized.lstrip("/")
    parts = normalized.split("/")
    if any(part in ("", ".", "..") for part in parts):
        return ""
    return normalized


def evaluate_controlled_map_evidence(
    sources: Iterable[tuple[str, str]], required: bool
) -> tuple[dict[str, Any], list[str]]:
    expected = normalize_engine_map_name(PBR_FIXTURE_STOCK_MAP)
    evidence: dict[str, Any] = {
        "required": required,
        "expected": expected if required else None,
        "observed": [],
        "status": "not-required",
    }
    if not required:
        return evidence, []
    observed: list[dict[str, str]] = []
    for source_name, source_text in sources:
        matches = ENGINE_MAP_PATTERN.findall(source_text)
        if matches:
            observed.append(
                {
                    "source": source_name,
                    "map": normalize_engine_map_name(matches[-1]),
                }
            )
    evidence["observed"] = observed
    failures: list[str] = []
    log_observation = next(
        (item for item in observed if item["source"] == "log"), None
    )
    if log_observation is None:
        failures.append("controlled fixture final engine Map evidence is missing from the log")
    for item in observed:
        if not item["map"] or item["map"] != expected:
            failures.append(
                "controlled fixture final engine Map differs: "
                f"{item['source']}={item['map'] or 'malformed'} expected={expected}"
            )
    evidence["status"] = "pass" if not failures else "fail"
    return evidence, failures


def evaluate_pbr_fixture_evidence(
    summary: dict[str, str], required: bool, render_api: str
) -> tuple[dict[str, Any], list[str]]:
    """Fail closed when the controlled PBR fixture did not get a real owner."""
    if render_api not in ("gl", "vk"):
        raise ValueError(f"unsupported PBR fixture evidence API: {render_api!r}")
    evidence: dict[str, Any] = {
        "required": required,
        "renderApi": render_api,
        "status": "not-required",
    }
    if not required:
        return evidence, []

    common_telemetry_specs = (
        (
            "pbrResources",
            "pbrMaterialResources",
            "PBR material resources",
            ("records", "resourceReady", "modernReady", "packed", "fallback"),
        ),
    )
    gl_telemetry_specs = (
        (
            "modernVisible",
            "modernVisibleFrame",
            "Modern visible frame",
            (
                "req",
                "exec",
                "resources",
                "program",
                "source",
                "hybrid",
                "backBuffer",
                "composed",
            ),
        ),
        (
            "drawPlan",
            "modernGLExecutor",
            "Modern GL executor",
            (
                "drawPlan",
                "planDraws",
                "planFallback",
                "pbrOwners",
                "pbrConsumed",
            ),
        ),
        (
            "forwardPlus",
            "modernForwardPlus",
            "Modern forward+",
            (
                "req",
                "exec",
                "resources",
                "sceneColor",
                "sceneDepth",
                "program",
                "cluster",
                "draws",
                "fallback",
            ),
        ),
    )
    telemetry_specs = common_telemetry_specs + (
        gl_telemetry_specs if render_api == "gl" else ()
    )
    failures: list[str] = []
    fields_by_name: dict[str, dict[str, int]] = {}
    telemetry_present: dict[str, bool] = {}
    for evidence_name, summary_name, display_name, field_names in telemetry_specs:
        line = summary.get(summary_name, "")
        line_pattern = PBR_TELEMETRY_PATTERNS[summary_name]
        line_is_exact = bool(line) and line_pattern.fullmatch(line) is not None
        telemetry_present[evidence_name] = line_is_exact
        fields: dict[str, int] = {}
        for name in field_names:
            field_match = re.search(rf"\b{re.escape(name)}=(-?\d+)\b", line)
            if field_match is not None:
                fields[name] = int(field_match.group(1))
        fields_by_name[evidence_name] = fields
        evidence[evidence_name] = fields
        if not line:
            failures.append(f"PBR fixture {display_name} telemetry missing")
        elif not line_is_exact:
            failures.append(f"PBR fixture {display_name} telemetry is malformed")

    common_checks = (
        ("pbrResources", "records", lambda value: value > 0, "PBR records>0"),
        ("pbrResources", "resourceReady", lambda value: value > 0, "PBR resourceReady>0"),
        ("pbrResources", "modernReady", lambda value: value > 0, "PBR modernReady>0"),
        ("pbrResources", "packed", lambda value: value > 0, "PBR packed resources>0"),
        ("pbrResources", "fallback", lambda value: value == 0, "PBR fallback=0"),
    )
    gl_checks = (
        ("modernVisible", "req", lambda value: value == 1, "modern visible req=1"),
        ("modernVisible", "exec", lambda value: value == 1, "modern visible exec=1"),
        ("modernVisible", "resources", lambda value: value == 1, "modern visible resources=1"),
        ("modernVisible", "program", lambda value: value == 1, "modern visible program=1"),
        ("modernVisible", "source", lambda value: value == 1, "modern visible source=1"),
        ("modernVisible", "hybrid", lambda value: value == 1, "modern visible hybrid=1"),
        ("modernVisible", "backBuffer", lambda value: value == 1, "modern visible backBuffer=1"),
        ("modernVisible", "composed", lambda value: value > 0, "modern visible composed>0"),
        ("drawPlan", "drawPlan", lambda value: value == 1, "draw plan ready=1"),
        ("drawPlan", "planDraws", lambda value: value > 0, "draw-plan draws>0"),
        ("drawPlan", "pbrOwners", lambda value: value > 0, "clustered PBR owners>0"),
        ("drawPlan", "pbrConsumed", lambda value: value > 0, "clustered PBR consumed interactions>0"),
        ("drawPlan", "planFallback", lambda value: value == 0, "draw-plan fallback=0"),
        ("forwardPlus", "req", lambda value: value == 1, "forward+ req=1"),
        ("forwardPlus", "exec", lambda value: value == 1, "forward+ exec=1"),
        ("forwardPlus", "resources", lambda value: value == 1, "forward+ resources=1"),
        ("forwardPlus", "sceneColor", lambda value: value == 1, "forward+ sceneColor=1"),
        ("forwardPlus", "sceneDepth", lambda value: value == 1, "forward+ sceneDepth=1"),
        ("forwardPlus", "program", lambda value: value == 1, "forward+ program=1"),
        ("forwardPlus", "cluster", lambda value: value == 1, "forward+ cluster=1"),
        ("forwardPlus", "draws", lambda value: value > 0, "forward+ draws>0"),
        ("forwardPlus", "fallback", lambda value: value == 0, "forward+ fallback=0"),
    )
    checks = common_checks + (gl_checks if render_api == "gl" else ())
    if (
        render_api == "gl"
        and telemetry_present.get("drawPlan", False)
        and not summary.get("modernGLExecutor", "").startswith(
            "Modern GL executor: available, "
        )
    ):
        failures.append("PBR fixture Modern GL executor is not available")
    for telemetry_name, field_name, accepted, requirement in checks:
        if not telemetry_present[telemetry_name]:
            continue
        fields = fields_by_name[telemetry_name]
        if field_name not in fields:
            failures.append(f"PBR fixture {requirement} (missing)")
            continue
        value = fields[field_name]
        if not accepted(value):
            failures.append(f"PBR fixture {requirement} (got {value})")

    if render_api == "vk":
        marker = summary.get("vulkanPackedPBR", "")
        match = re.fullmatch(
            r"Vulkan: native PBR direct interactions active \((\d+) draws\)",
            marker,
        )
        draws = int(match.group(1)) if match else None
        evidence["vulkan"] = {} if draws is None else {"draws": draws}
        if match is None:
            failures.append("PBR fixture Vulkan native packed-PBR draw telemetry missing")
        elif draws <= 0:
            failures.append(f"PBR fixture Vulkan native packed-PBR draws>0 (got {draws})")

    evidence["status"] = "pass" if not failures else "fail"
    return evidence, failures


def summary_float(summary: dict[str, str], key: str) -> float | None:
    value = summary.get(key)
    if value is None:
        return None
    try:
        return float(value)
    except ValueError:
        return None


def shared_interaction_fields(summary: dict[str, str]) -> dict[str, str]:
    line = summary.get("sharedInteraction", "")
    return {
        key: value
        for key, value in re.findall(r"\b([A-Za-z]+)=([^\s]+)", line)
    }


def shared_fog_blend_fields(summary: dict[str, str]) -> dict[str, str]:
    return {
        key: value
        for key, value in re.findall(
            r"\b([A-Za-z]+)=([^\s]+)", summary.get("sharedFogBlend", "")
        )
    }


def shared_fog_blend_backend_records(
    summary: dict[str, str]
) -> dict[str, dict[str, str]]:
    records: dict[str, dict[str, str]] = {}
    for line in summary.get("sharedFogBlendBackends", "").splitlines():
        fields = {
            key: value
            for key, value in re.findall(r"\b([A-Za-z]+)=([^\s]+)", line)
        }
        backend = fields.get("backend")
        if backend:
            records[backend] = fields
    return records


def shared_fog_blend_view_records(
    summary: dict[str, str]
) -> list[dict[str, int | str]]:
    records: list[dict[str, int | str]] = []
    pattern = re.compile(
        r"classicFogBlendDomain view\[(?P<index>\d+)\].*?"
        r"\bready=(?P<ready>\d+)\s+failure=(?P<failure>[^\s]+)\s+"
        r"detail=(?P<detail>-?\d+).*?"
        r"\bGL=(?P<glOutcome>\d+)/(?P<glFailure>[^/\s]+)/(?P<glDetail>-?\d+)\s+"
        r"Vulkan=(?P<vkOutcome>\d+)/(?P<vkFailure>[^/\s]+)/(?P<vkDetail>-?\d+)"
    )
    for line in summary.get("sharedFogBlendViews", "").splitlines():
        match = pattern.search(line)
        if match is None:
            continue
        fields: dict[str, int | str] = match.groupdict()
        for name in (
            "index",
            "ready",
            "detail",
            "glOutcome",
            "glDetail",
            "vkOutcome",
            "vkDetail",
        ):
            fields[name] = int(str(fields[name]))
        records.append(fields)
    return records


def shared_interaction_shadow_counts(
    fields: dict[str, str]
) -> tuple[int, int, int, int, int, int] | None:
    shadow = re.fullmatch(r"(\d+)/(\d+)/(\d+)\+(\d+)", fields.get("shadow", ""))
    volumes = re.fullmatch(r"(\d+)\+(\d+)", fields.get("volumes", ""))
    if shadow is None or volumes is None:
        return None
    return tuple(int(value) for value in (*shadow.groups(), *volumes.groups()))


def shared_interaction_map_counts(
    fields: dict[str, str]
) -> tuple[int, int, int, int, int] | None:
    maps = re.fullmatch(r"(\d+)\+(\d+)", fields.get("maps", ""))
    modes = re.fullmatch(r"(\d+)\+(\d+)", fields.get("modes", ""))
    csm = re.fullmatch(r"\d+", fields.get("csm", ""))
    if maps is None or modes is None or csm is None:
        return None
    return tuple(
        int(value) for value in (*maps.groups(), *modes.groups(), csm.group())
    )


def shared_interaction_map_records(text: str) -> list[dict[str, int | str]]:
    pattern = re.compile(
        r"^Renderer shared interaction map view\[(\d+)\] light=(\d+) "
        r"receiver=(local|global) mode=(none|stencil|projected|point|hybrid) "
        r"class=(point|parallel|global|projected) cascades=(\d+) "
        r"alias=(\d+) plan=([0-9a-fA-F]{16}) generation=(\d+) "
        r"casters=(\d+)\+(\d+) features=(\d+)\+(\d+)\+(\d+)\+(\d+) "
        r"hash=([0-9a-fA-F]{16})$"
    )
    records: list[dict[str, int | str]] = []
    for line in text.splitlines():
        match = pattern.fullmatch(line.strip())
        if match is None:
            continue
        (
            view,
            light,
            receiver,
            mode,
            light_class,
            cascades,
            alias,
            plan,
            generation,
            casters,
            supplements,
            static_casters,
            dynamic_casters,
            alpha_casters,
            translucent_casters,
            semantic_hash,
        ) = match.groups()
        records.append(
            {
                "view": int(view),
                "light": int(light),
                "receiver": receiver,
                "mode": mode,
                "class": light_class,
                "cascades": int(cascades),
                "alias": int(alias),
                "plan": int(plan, 16),
                "generation": int(generation),
                "casters": int(casters),
                "supplements": int(supplements),
                "static": int(static_casters),
                "dynamic": int(dynamic_casters),
                "alpha": int(alpha_casters),
                "translucent": int(translucent_casters),
                "hash": int(semantic_hash, 16),
            }
        )
    return records


def shared_interaction_backend_counts(
    value: str,
) -> tuple[int, str, int, int, int, int, int, int, int, int, int] | None:
    match = re.fullmatch(
        r"(\d+)/([^/]+)/(-?\d+)/(\d+)\+(\d+)/"
        r"(\d+)\+(\d+)\+(\d+)\+(\d+)/(\d+)\+(\d+)",
        value,
    )
    if match is None:
        return None
    (
        outcome,
        failure,
        detail,
        drawn_primitives,
        noop_primitives,
        submitted_shadow_casters,
        noop_shadow_casters,
        logical_volume_draws,
        preload_volume_draws,
        shadow_map_passes,
        hybrid_passes,
    ) = match.groups()
    return (
        int(outcome),
        failure,
        int(detail),
        int(drawn_primitives),
        int(noop_primitives),
        int(submitted_shadow_casters),
        int(noop_shadow_casters),
        int(logical_volume_draws),
        int(preload_volume_draws),
        int(shadow_map_passes),
        int(hybrid_passes),
    )


# The controlled map-budget scene exposes four shadow lights: one projected
# pass followed by three point-light passes. Its one-update admission limit
# rejects the second light in the GL map transaction: reason 24, one-based
# light detail 2,
# and no surface/primitive detail.
GL_MAP_BUDGET_FALLBACK_DETAIL = 2_402_000_000
CONTROLLED_MAP_BUDGET_COUNTS = (4, 0, 1, 3, 0)
VK_SHADOW_STATE_REJECT = 7
TRANSLUCENT_SHADOW_CHAIN_DETAILS = (6, 7)


def evaluate_shared_interaction_evidence(
    spec: RunSpec, summary: dict[str, str]
) -> list[str]:
    expectation = spec.interaction_expectation
    if expectation == "none":
        return []

    fields = shared_interaction_fields(summary)
    view_fields = {
        key: value
        for key, value in re.findall(
            r"\b([A-Za-z]+)=([^\s]+)", summary.get("sharedInteractionView", "")
        )
    }
    shadow_counts = shared_interaction_shadow_counts(fields)
    map_counts = shared_interaction_map_counts(fields)
    map_records = shared_interaction_map_records(
        summary.get("sharedInteractionMaps", "")
    )
    failures: list[str] = []
    if not fields:
        return ["shared interaction evidence line"]

    def integer(name: str) -> int | None:
        value = fields.get(name)
        try:
            return int(value) if value is not None else None
        except ValueError:
            return None

    requested = integer("requested")
    if expectation == "disabled":
        if requested != 0:
            failures.append(f"shared interaction requested={requested}")
        for name in (
            "prepared",
            "valid",
            "views",
            "ready",
            "fallback",
            "lights",
            "surfaces",
            "primitives",
            "draw",
            "noop",
        ):
            if integer(name) != 0:
                failures.append(
                    f"shared interaction {name}={fields.get(name, 'missing')}"
                )
        if shadow_counts != (0, 0, 0, 0, 0, 0):
            failures.append(
                "shared interaction disabled shadow accounting="
                f"{fields.get('shadow', 'missing')}/"
                f"{fields.get('volumes', 'missing')}"
            )
        if map_counts != (0, 0, 0, 0, 0) or map_records:
            failures.append(
                "shared interaction disabled map accounting="
                f"{fields.get('maps', 'missing')}/"
                f"{fields.get('modes', 'missing')}/"
                f"{fields.get('csm', 'missing')} records={len(map_records)}"
            )
        if fields.get("status") != "empty":
            failures.append(
                f"shared interaction status={fields.get('status', 'missing')}"
            )
        for backend_name in ("GL", "VK"):
            if fields.get(backend_name) != "0/0/0":
                failures.append(
                    "shared interaction "
                    f"{backend_name} coverage={fields.get(backend_name, 'missing')}"
                )
        return failures

    for name in ("requested", "prepared"):
        if integer(name) != 1:
            failures.append(f"shared interaction {name}={fields.get(name, 'missing')}")

    backend_name = "VK" if spec.expected_backend == "vulkan" else "GL"
    backend_value = fields.get(backend_name, "")
    backend_match = re.fullmatch(r"(\d+)/(\d+)/(\d+)", backend_value)
    if backend_match is None:
        failures.append(f"shared interaction {backend_name} coverage={backend_value or 'missing'}")
        owned_views = fallback_views = mismatches = -1
    else:
        owned_views, fallback_views, mismatches = (
            int(value) for value in backend_match.groups()
        )
    inactive_backend_name = "GL" if backend_name == "VK" else "VK"
    inactive_backend_value = fields.get(inactive_backend_name, "")
    if inactive_backend_value != "0/0/0":
        failures.append(
            "shared interaction inactive backend coverage="
            f"{inactive_backend_name}:{inactive_backend_value or 'missing'}"
        )
    inactive_backend_counts = shared_interaction_backend_counts(
        view_fields.get(inactive_backend_name, "")
    )
    expected_inactive_backend_counts = (
        0,
        "none",
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    )
    if inactive_backend_counts != expected_inactive_backend_counts:
        failures.append(
            "shared interaction inactive backend execution="
            f"{inactive_backend_name}:{view_fields.get(inactive_backend_name, 'missing')}"
        )

    if expectation == "owned":
        if integer("valid") != 1:
            failures.append(
                f"shared interaction valid={fields.get('valid', 'missing')}"
            )
        for name in ("ready", "lights", "surfaces", "primitives", "draw"):
            value = integer(name)
            if value is None or value <= 0:
                failures.append(f"shared interaction {name}={fields.get(name, 'missing')}")
        if integer("fallback") != 0:
            failures.append(
                f"shared interaction fallback={fields.get('fallback', 'missing')}"
            )
        if fields.get("status") != "ready":
            failures.append(
                f"shared interaction status={fields.get('status', 'missing')}"
            )
        ready_views = integer("ready")
        if integer("views") != ready_views:
            failures.append(
                "shared interaction complete-view readiness="
                f"{fields.get('ready', 'missing')}/{fields.get('views', 'missing')}"
            )
        primitive_count = integer("primitives")
        draw_count = integer("draw")
        noop_count = integer("noop")
        if (
            primitive_count is None
            or draw_count is None
            or noop_count is None
            or primitive_count != draw_count + noop_count
        ):
            failures.append(
                "shared interaction primitive reconciliation="
                f"{fields.get('primitives', 'missing')}/"
                f"{fields.get('draw', 'missing')}+{fields.get('noop', 'missing')}"
            )
        if (
            ready_views is None
            or owned_views != ready_views
            or fallback_views != 0
            or mismatches != 0
        ):
            failures.append(f"shared interaction {backend_name} coverage={backend_value}")
        shadow_expectation = spec.interaction_shadow_expectation
        drawable_shadow_casters = None
        noop_shadow_casters = None
        logical_volume_draws = None
        preload_volume_draws = None
        if shadow_counts is None:
            failures.append("shared interaction shadow/volume counters=missing")
        else:
            (
                shadow_lights,
                shadow_casters,
                drawable_shadow_casters,
                noop_shadow_casters,
                logical_volume_draws,
                preload_volume_draws,
            ) = shadow_counts
            if shadow_casters != drawable_shadow_casters + noop_shadow_casters:
                failures.append(
                    "shared interaction shadow caster reconciliation="
                    f"{shadow_casters}/{drawable_shadow_casters}+"
                    f"{noop_shadow_casters}"
                )
            if preload_volume_draws > logical_volume_draws:
                failures.append(
                    "shared interaction volume preload reconciliation="
                    f"{logical_volume_draws}+{preload_volume_draws}"
                )
            light_count = integer("lights")
            if light_count is None or shadow_lights > light_count:
                failures.append(
                    "shared interaction shadow light reconciliation="
                    f"{shadow_lights}/{fields.get('lights', 'missing')}"
                )
            if shadow_expectation == "none":
                if shadow_counts != (0, 0, 0, 0, 0, 0):
                    failures.append(
                        "shared interaction unshadowed accounting="
                        f"{fields.get('shadow', 'missing')}/"
                        f"{fields.get('volumes', 'missing')}"
                    )
            elif shadow_expectation == "stencil":
                if (
                    shadow_lights <= 0
                    or shadow_casters <= 0
                    or drawable_shadow_casters <= 0
                    or logical_volume_draws <= 0
                ):
                    failures.append(
                        "shared interaction stencil ownership="
                        f"{fields.get('shadow', 'missing')}/"
                        f"{fields.get('volumes', 'missing')}"
                    )
                if view_fields.get("mode") != "stencil":
                    failures.append(
                        "shared interaction stencil view mode="
                        f"{view_fields.get('mode', 'missing')}"
                    )
                if view_fields.get("failure") != "none":
                    failures.append(
                        "shared interaction stencil view failure="
                        f"{view_fields.get('failure', 'missing')}"
                    )
        if map_counts is None:
            failures.append("shared interaction map/mode counters=missing")
            map_passes = hybrid_passes = projected_passes = point_passes = csm_passes = -1
        else:
            (
                map_passes,
                hybrid_passes,
                projected_passes,
                point_passes,
                csm_passes,
            ) = map_counts
            if projected_passes + point_passes != map_passes:
                failures.append(
                    "shared interaction map class reconciliation="
                    f"{map_passes}/{projected_passes}+{point_passes}"
                )
            if hybrid_passes > map_passes or csm_passes > projected_passes:
                failures.append(
                    "shared interaction map subtype reconciliation="
                    f"hybrid={hybrid_passes}/{map_passes} "
                    f"csm={csm_passes}/{projected_passes}"
                )

            record_keys = {
                (record["view"], record["light"], record["receiver"])
                for record in map_records
            }
            if len(map_records) != map_passes or len(record_keys) != map_passes:
                failures.append(
                    "shared interaction per-map reconciliation="
                    f"records={len(map_records)} unique={len(record_keys)} "
                    f"passes={map_passes}"
                )
            record_projected = sum(
                record["class"] != "point" for record in map_records
            )
            record_point = sum(
                record["class"] == "point" for record in map_records
            )
            record_csm = sum(
                record["class"] != "point" and int(record["cascades"]) > 1
                for record in map_records
            )
            record_hybrid = sum(
                record["mode"] == "hybrid" for record in map_records
            )
            if (
                record_projected,
                record_point,
                record_csm,
                record_hybrid,
            ) != (
                projected_passes,
                point_passes,
                csm_passes,
                hybrid_passes,
            ):
                failures.append(
                    "shared interaction per-map subtype evidence="
                    f"{record_projected}+{record_point}/csm={record_csm}/"
                    f"hybrid={record_hybrid} expected="
                    f"{projected_passes}+{point_passes}/csm={csm_passes}/"
                    f"hybrid={hybrid_passes}"
                )
            for record in map_records:
                cascades = int(record["cascades"])
                feature_values = tuple(
                    int(record[name])
                    for name in ("static", "dynamic", "alpha", "translucent")
                )
                if (
                    int(record["casters"]) <= 0
                    or int(record["generation"]) <= 0
                    or int(record["plan"]) == 0
                    or int(record["hash"]) == 0
                    or int(record["alias"]) not in (0, 1)
                    or any(value not in (0, 1) for value in feature_values)
                    or not (int(record["static"]) or int(record["dynamic"]))
                    or int(record["translucent"]) != 0
                    or (record["class"] == "point" and cascades != 6)
                    or (record["class"] != "point" and not 1 <= cascades <= 4)
                    or (
                        record["mode"] == "hybrid"
                        and int(record["supplements"]) <= 0
                    )
                ):
                    failures.append(
                        "shared interaction invalid per-map record="
                        f"{record}"
                    )
                if int(record["alias"]) == 1:
                    owner_key = (record["view"], record["light"], "local")
                    if record["receiver"] != "global" or owner_key not in record_keys:
                        failures.append(
                            "shared interaction invalid map alias="
                            f"{record}"
                        )

            if map_passes > 0:
                if shadow_counts is None:
                    failures.append(
                        "shared interaction mapped caster accounting=missing"
                    )
                else:
                    per_light_record_casters: dict[tuple[int, int], int] = {}
                    for record in map_records:
                        light_key = (int(record["view"]), int(record["light"]))
                        per_light_record_casters[light_key] = max(
                            per_light_record_casters.get(light_key, 0),
                            int(record["casters"])
                            + int(record["supplements"]),
                        )
                    record_caster_floor = sum(per_light_record_casters.values())
                    if (
                        shadow_counts[0] <= 0
                        or shadow_counts[1] <= 0
                        or len(per_light_record_casters) > shadow_counts[0]
                        or record_caster_floor > shadow_counts[1]
                    ):
                        failures.append(
                            "shared interaction mapped caster accounting="
                            f"recordsMin={record_caster_floor} lights="
                            f"{len(per_light_record_casters)} shadow="
                            f"{fields.get('shadow', 'missing')}"
                        )

            if shadow_expectation == "dynamic" and not any(
                int(record["dynamic"]) == 1 for record in map_records
            ):
                failures.append(
                    "shared interaction dynamic caster feature missing="
                    f"{map_records}"
                )
            if shadow_expectation == "perforated" and not any(
                int(record["alpha"]) == 1 for record in map_records
            ):
                failures.append(
                    "shared interaction perforated caster feature missing="
                    f"{map_records}"
                )

            allowed_modes = {
                "none": {"none"},
                "stencil": {"stencil"},
                "projected": {"projected", "hybrid"},
                "point": {"point", "hybrid"},
                "csm": {"projected", "hybrid"},
                "mapped": {"hybrid"},
                "mixed": {"hybrid"},
                "dynamic": {"projected", "point", "hybrid"},
                "perforated": {"projected", "point", "hybrid"},
                "hybrid": {"hybrid"},
            }.get(shadow_expectation)
            if (
                allowed_modes is not None
                and view_fields.get("mode") not in allowed_modes
            ):
                failures.append(
                    "shared interaction shadow view mode="
                    f"{view_fields.get('mode', 'missing')} "
                    f"expected={sorted(allowed_modes)}"
                )
            if view_fields.get("failure") != "none":
                failures.append(
                    "shared interaction shadow view failure="
                    f"{view_fields.get('failure', 'missing')}"
                )

            expected_map_shape = {
                "none": (0, 0, 0, 0, 0),
                "stencil": (0, 0, 0, 0, 0),
            }.get(shadow_expectation)
            if expected_map_shape is not None and map_counts != expected_map_shape:
                failures.append(
                    "shared interaction unexpected mapped work="
                    f"{map_counts} expected={expected_map_shape}"
                )
            elif shadow_expectation == "projected" and not (
                map_passes > 0
                and projected_passes > 0
                and csm_passes == 0
                and any(
                    record["class"] == "projected"
                    and int(record["cascades"]) == 1
                    for record in map_records
                )
            ):
                failures.append(f"shared interaction projected map shape={map_counts}")
            elif shadow_expectation == "point" and not (
                map_passes > 0
                and point_passes > 0
            ):
                failures.append(f"shared interaction point map shape={map_counts}")
            elif shadow_expectation == "csm" and not (
                map_passes > 0
                and projected_passes > 0
                and csm_passes > 0
            ):
                failures.append(f"shared interaction CSM map shape={map_counts}")
            elif shadow_expectation == "mapped" and not (
                map_passes > 0
                and hybrid_passes == 0
                and projected_passes > 0
                and point_passes > 0
                and csm_passes == 0
            ):
                failures.append(f"shared interaction mapped-light shape={map_counts}")
            elif shadow_expectation == "mixed" and not (
                map_passes > 0
                and hybrid_passes == 0
                and projected_passes > 0
                and point_passes == 0
                and csm_passes == 0
                and shadow_counts is not None
                and shadow_counts[4] > 0
                and shadow_counts[0]
                > len({int(record["light"]) for record in map_records})
            ):
                failures.append(
                    "shared interaction map/stencil mix="
                    f"maps={map_counts} volumes="
                    f"{fields.get('volumes', 'missing')} shadowLights="
                    f"{fields.get('shadow', 'missing')} mappedLights="
                    f"{len({int(record['light']) for record in map_records})}"
                )
            elif shadow_expectation in ("dynamic", "perforated") and not (
                map_passes > 0 and projected_passes + point_passes > 0
            ):
                failures.append(
                    f"shared interaction {shadow_expectation} mapped ownership="
                    f"{map_counts}"
                )
            elif shadow_expectation == "hybrid" and not (
                map_passes > 0
                and hybrid_passes > 0
                and shadow_counts is not None
                and shadow_counts[4] > 0
            ):
                failures.append(
                    "shared interaction same-light hybrid="
                    f"maps={map_counts} volumes="
                    f"{fields.get('volumes', 'missing')}"
                )
            backend_counts = shared_interaction_backend_counts(
                view_fields.get(backend_name, "")
            )
            if backend_counts is None:
                failures.append(
                    f"shared interaction {backend_name} view coverage="
                    f"{view_fields.get(backend_name, 'missing')}"
                )
            else:
                (
                    backend_outcome,
                    backend_failure,
                    backend_detail,
                    backend_drawn_primitives,
                    backend_noop_primitives,
                    backend_shadow_casters,
                    backend_noop_shadow_casters,
                    backend_logical_volume_draws,
                    backend_preload_volume_draws,
                    backend_shadow_map_passes,
                    backend_hybrid_passes,
                ) = backend_counts
                if (
                    backend_outcome != 1
                    or backend_failure != "none"
                    or backend_detail != 0
                ):
                    failures.append(
                        f"shared interaction {backend_name} view outcome="
                        f"{view_fields.get(backend_name, 'missing')}"
                    )
                expected_backend_counts = (
                    draw_count,
                    noop_count,
                    drawable_shadow_casters,
                    noop_shadow_casters,
                    logical_volume_draws,
                    preload_volume_draws,
                    map_passes,
                    hybrid_passes,
                )
                actual_backend_counts = (
                    backend_drawn_primitives,
                    backend_noop_primitives,
                    backend_shadow_casters,
                    backend_noop_shadow_casters,
                    backend_logical_volume_draws,
                    backend_preload_volume_draws,
                    backend_shadow_map_passes,
                    backend_hybrid_passes,
                )
                if actual_backend_counts != expected_backend_counts:
                    failures.append(
                        f"shared interaction {backend_name} execution="
                        f"{actual_backend_counts} expected={expected_backend_counts}"
                    )
    elif expectation == "fallback":
        valid = integer("valid")
        ready_count = integer("ready")
        core_fallback_count = integer("fallback")
        view_count = integer("views")
        if valid == 0:
            if ready_count != 0 or (core_fallback_count or 0) <= 0:
                failures.append(
                    "shared interaction whole-view core fallback accounting="
                    f"{fields.get('ready', 'missing')}/"
                    f"{fields.get('fallback', 'missing')}"
                )
            if fields.get("status") != "fallback":
                failures.append(
                    f"shared interaction status={fields.get('status', 'missing')}"
                )
            for name in ("lights", "surfaces", "primitives", "draw", "noop"):
                if integer(name) != 0:
                    failures.append(
                        f"shared interaction rollback {name}="
                        f"{fields.get(name, 'missing')}"
                    )
            if shadow_counts != (0, 0, 0, 0, 0, 0):
                failures.append(
                    "shared interaction rollback shadow accounting="
                    f"{fields.get('shadow', 'missing')}/"
                    f"{fields.get('volumes', 'missing')}"
                )
            if map_counts != (0, 0, 0, 0, 0) or map_records:
                failures.append(
                    "shared interaction rollback map accounting="
                    f"{fields.get('maps', 'missing')}/"
                    f"{fields.get('modes', 'missing')}/"
                    f"{fields.get('csm', 'missing')} records={len(map_records)}"
                )
            if view_count != core_fallback_count:
                failures.append(
                    "shared interaction complete-view fallback="
                    f"{fields.get('fallback', 'missing')}/"
                    f"{fields.get('views', 'missing')}"
                )
        elif valid == 1:
            if (
                ready_count is None
                or ready_count <= 0
                or core_fallback_count != 0
                or view_count != ready_count
            ):
                failures.append(
                    "shared interaction backend fallback preparation="
                    f"{fields.get('ready', 'missing')}/"
                    f"{fields.get('fallback', 'missing')}/"
                    f"{fields.get('views', 'missing')}"
                )
            if fields.get("status") != "ready":
                failures.append(
                    f"shared interaction status={fields.get('status', 'missing')}"
                )
            primitive_count = integer("primitives")
            draw_count = integer("draw")
            noop_count = integer("noop")
            if (
                primitive_count is None
                or draw_count is None
                or noop_count is None
                or primitive_count != draw_count + noop_count
            ):
                failures.append(
                    "shared interaction fallback primitive plan="
                    f"{fields.get('primitives', 'missing')}/"
                    f"{fields.get('draw', 'missing')}+"
                    f"{fields.get('noop', 'missing')}"
                )
            if shadow_counts is None:
                failures.append("shared interaction fallback shadow plan=missing")
            else:
                _, casters, drawable, noop, logical, preload = shadow_counts
                if casters != drawable + noop or preload > logical:
                    failures.append(
                        "shared interaction fallback shadow plan="
                        f"{fields.get('shadow', 'missing')}/"
                        f"{fields.get('volumes', 'missing')}"
                    )
            if map_counts is None:
                failures.append("shared interaction fallback map plan=missing")
            else:
                maps, hybrid, projected, point, csm = map_counts
                fallback_record_keys = {
                    (record["view"], record["light"], record["receiver"])
                    for record in map_records
                }
                fallback_record_shape = (
                    sum(record["class"] != "point" for record in map_records),
                    sum(record["class"] == "point" for record in map_records),
                    sum(
                        record["class"] != "point"
                        and int(record["cascades"]) > 1
                        for record in map_records
                    ),
                    sum(record["mode"] == "hybrid" for record in map_records),
                )
                if (
                    projected + point != maps
                    or hybrid > maps
                    or csm > projected
                    or len(map_records) != maps
                    or len(fallback_record_keys) != maps
                    or fallback_record_shape != (projected, point, csm, hybrid)
                ):
                    failures.append(
                        "shared interaction fallback mapped plan="
                        f"{map_counts} records={len(map_records)}/"
                        f"{len(fallback_record_keys)} shape={fallback_record_shape}"
                    )
                fallback_light_count = integer("lights")
                fallback_view_count = integer("views")
                for record in map_records:
                    cascades = int(record["cascades"])
                    feature_values = tuple(
                        int(record[name])
                        for name in ("static", "dynamic", "alpha", "translucent")
                    )
                    if (
                        int(record["casters"]) <= 0
                        or int(record["generation"]) <= 0
                        or int(record["plan"]) == 0
                        or int(record["hash"]) == 0
                        or int(record["alias"]) not in (0, 1)
                        or any(value not in (0, 1) for value in feature_values)
                        or not (int(record["static"]) or int(record["dynamic"]))
                        or int(record["translucent"]) != 0
                        or (record["class"] == "point" and cascades != 6)
                        or (record["class"] != "point" and not 1 <= cascades <= 4)
                        or (
                            record["mode"] == "hybrid"
                            and int(record["supplements"]) <= 0
                        )
                        or fallback_light_count is None
                        or not 0 <= int(record["light"]) < fallback_light_count
                        or fallback_view_count is None
                        or not 0 <= int(record["view"]) < fallback_view_count
                    ):
                        failures.append(
                            "shared interaction invalid fallback per-map record="
                            f"{record}"
                        )
                    if int(record["alias"]) == 1:
                        owner_key = (record["view"], record["light"], "local")
                        if (
                            record["receiver"] != "global"
                            or owner_key not in fallback_record_keys
                        ):
                            failures.append(
                                "shared interaction invalid fallback map alias="
                                f"{record}"
                            )
                if maps > 0:
                    fallback_per_light_casters: dict[tuple[int, int], int] = {}
                    for record in map_records:
                        light_key = (int(record["view"]), int(record["light"]))
                        fallback_per_light_casters[light_key] = max(
                            fallback_per_light_casters.get(light_key, 0),
                            int(record["casters"])
                            + int(record["supplements"]),
                        )
                    fallback_record_caster_floor = sum(
                        fallback_per_light_casters.values()
                    )
                    if (
                        shadow_counts is None
                        or shadow_counts[0] < len(fallback_per_light_casters)
                        or shadow_counts[1] <= 0
                        or fallback_record_caster_floor > shadow_counts[1]
                    ):
                        failures.append(
                            "shared interaction fallback mapped caster accounting="
                            f"recordsMin={fallback_record_caster_floor} lights="
                            f"{len(fallback_per_light_casters)} shadow="
                            f"{fields.get('shadow', 'missing')}"
                        )
        else:
            failures.append(
                f"shared interaction valid={fields.get('valid', 'missing')}"
            )

        backend_counts = shared_interaction_backend_counts(
            view_fields.get(backend_name, "")
        )
        if backend_counts is None:
            failures.append(
                f"shared interaction {backend_name} fallback detail="
                f"{view_fields.get(backend_name, 'missing')}"
            )
        else:
            (
                backend_outcome,
                backend_failure,
                backend_detail,
                backend_drawn,
                backend_noop,
                backend_shadow,
                backend_shadow_noop,
                backend_logical,
                backend_preload,
                backend_maps,
                backend_hybrid,
            ) = backend_counts
            if backend_outcome != 2 or backend_failure in ("none", "unknown"):
                failures.append(
                    f"shared interaction {backend_name} named fallback="
                    f"{view_fields.get(backend_name, 'missing')}"
                )
            shadow_expectation = spec.interaction_shadow_expectation
            if shadow_expectation == "translucent-fallback":
                try:
                    view_failure_detail = int(view_fields.get("detail", ""))
                except ValueError:
                    view_failure_detail = -1
                if (
                    valid != 0
                    or view_fields.get("failure") != "shadowMap"
                    or view_failure_detail not in TRANSLUCENT_SHADOW_CHAIN_DETAILS
                    or backend_failure != "shadowMap"
                    or backend_detail != view_failure_detail
                ):
                    failures.append(
                        "shared interaction translucent-moment fallback="
                        f"view={view_fields.get('failure', 'missing')}/"
                        f"{view_fields.get('detail', 'missing')} backend="
                        f"{backend_name}:{backend_failure}/{backend_detail}"
                    )
            elif shadow_expectation == "map-budget-fallback":
                expected_backend_failure = (
                    backend_failure == "backendRejected"
                    and (backend_detail & 0xFFFFFFFF)
                    == GL_MAP_BUDGET_FALLBACK_DETAIL
                    if backend_name == "GL"
                    else backend_failure == "shadowMap"
                    and backend_detail == VK_SHADOW_STATE_REJECT
                )
                if (
                    valid != 1
                    or view_fields.get("failure") != "none"
                    or view_fields.get("detail") != "0"
                    or map_counts is None
                    or map_counts != CONTROLLED_MAP_BUDGET_COUNTS
                    or not expected_backend_failure
                ):
                    failures.append(
                        "shared interaction map-budget admission fallback="
                        f"view={view_fields.get('failure', 'missing')}/"
                        f"{view_fields.get('detail', 'missing')} maps={map_counts} "
                        f"backend={backend_name}:{backend_failure}/{backend_detail}"
                    )
            if any(
                value != 0
                for value in (
                    backend_drawn,
                    backend_noop,
                    backend_shadow,
                    backend_shadow_noop,
                    backend_logical,
                    backend_preload,
                    backend_maps,
                    backend_hybrid,
                )
            ):
                failures.append(
                    f"shared interaction {backend_name} atomic fallback="
                    f"{view_fields.get(backend_name, 'missing')}"
                )
        if (
            view_count is None
            or owned_views != 0
            or fallback_views != view_count
            or mismatches != 0
        ):
            failures.append(f"shared interaction {backend_name} coverage={backend_value}")
    else:
        failures.append(f"unknown shared interaction expectation {expectation}")
    return failures


def evaluate_shared_fog_blend_evidence(
    spec: RunSpec, summary: dict[str, str]
) -> list[str]:
    expectation = spec.fog_blend_expectation
    if expectation == "none":
        return []

    fields = shared_fog_blend_fields(summary)
    backends = shared_fog_blend_backend_records(summary)
    view_records = shared_fog_blend_view_records(summary)
    if not fields:
        return ["classicFogBlendDomain evidence line"]

    failures: list[str] = []

    def integer(record: dict[str, str], name: str) -> int | None:
        try:
            return int(record[name])
        except (KeyError, ValueError):
            return None

    aggregate_counts = (
        "overflow",
        "views",
        "ready",
        "fallback",
        "lights",
        "fog",
        "blend",
        "noopLights",
        "surfaces",
        "global",
        "local",
        "stages",
        "active",
        "inactive",
        "noopStages",
        "primitives",
        "fogReceivers",
        "fogCaps",
        "blendDraws",
        "noop",
        "textures",
    )
    backend_counts = (
        "ownedViews",
        "fallbackViews",
        "ownedFogReceivers",
        "ownedFogCaps",
        "ownedBlend",
        "ownedNoops",
        "ownedNoopStages",
        "ownedNoopLights",
        "mismatches",
        "duplicate",
        "untracked",
    )
    if expectation == "disabled":
        for name in ("requested", "prepared", "frameValid", *aggregate_counts):
            if integer(fields, name) != 0:
                failures.append(
                    f"classicFogBlendDomain {name}={fields.get(name, 'missing')}"
                )
        for backend_name in ("GL", "Vulkan"):
            record = backends.get(backend_name, {})
            for name in backend_counts:
                if integer(record, name) != 0:
                    failures.append(
                        "classicFogBlendDomain disabled backend "
                        f"{backend_name} {name}={record.get(name, 'missing')}"
                    )
        return failures

    views = integer(fields, "views")
    active_backend = "Vulkan" if spec.render_api == "vk" else "GL"
    inactive_backend = "GL" if active_backend == "Vulkan" else "Vulkan"
    active = backends.get(active_backend, {})
    inactive = backends.get(inactive_backend, {})
    if expectation == "fallback":
        for name in ("requested", "prepared"):
            if integer(fields, name) != 1:
                failures.append(
                    f"classicFogBlendDomain {name}={fields.get(name, 'missing')}"
                )
        if integer(fields, "overflow") != 0 or views is None or views <= 0:
            failures.append(
                "classicFogBlendDomain fallback aggregate="
                f"overflow={fields.get('overflow', 'missing')} "
                f"views={fields.get('views', 'missing')}"
            )
        domain_fallback = integer(fields, "frameValid") == 0
        backend_fallback = integer(fields, "frameValid") == 1
        if domain_fallback:
            if (
                integer(fields, "ready") != 0
                or integer(fields, "fallback") != views
                or fields.get("status") != "fallback"
            ):
                failures.append(
                    "classicFogBlendDomain domain fallback state="
                    f"ready={fields.get('ready', 'missing')} "
                    f"fallback={fields.get('fallback', 'missing')} "
                    f"status={fields.get('status', 'missing')}"
                )
            for name in aggregate_counts[4:]:
                if integer(fields, name) != 0:
                    failures.append(
                        "classicFogBlendDomain domain fallback retained "
                        f"{name}={fields.get(name, 'missing')}"
                    )
        elif backend_fallback:
            if (
                integer(fields, "ready") != views
                or integer(fields, "fallback") != 0
                or fields.get("status") != "ready"
            ):
                failures.append(
                    "classicFogBlendDomain backend fallback state="
                    f"ready={fields.get('ready', 'missing')} "
                    f"fallback={fields.get('fallback', 'missing')} "
                    f"status={fields.get('status', 'missing')}"
                )
        else:
            failures.append(
                "classicFogBlendDomain fallback frameValid="
                f"{fields.get('frameValid', 'missing')}"
            )

        if integer(active, "ownedViews") != 0 or integer(
            active, "fallbackViews"
        ) != views:
            failures.append(
                f"classicFogBlendDomain {active_backend} fallback coverage"
            )
        for name in (
            "ownedFogReceivers",
            "ownedFogCaps",
            "ownedBlend",
            "ownedNoops",
            "ownedNoopStages",
            "ownedNoopLights",
        ):
            if integer(active, name) != 0:
                failures.append(
                    f"classicFogBlendDomain {active_backend} atomic fallback "
                    f"{name}={active.get(name, 'missing')}"
                )
        active_prefix = "vk" if active_backend == "Vulkan" else "gl"
        inactive_prefix = "gl" if active_prefix == "vk" else "vk"
        if len(view_records) != views:
            failures.append(
                "classicFogBlendDomain fallback view records="
                f"{len(view_records)}/{views}"
            )
        for record in view_records:
            active_failure = record.get(f"{active_prefix}Failure")
            active_detail = record.get(f"{active_prefix}Detail")
            valid_domain_failure = (
                domain_fallback
                and record.get("ready") == 0
                and record.get("failure") not in ("none", "unknown")
                and active_failure == record.get("failure")
                and active_detail == record.get("detail")
            )
            valid_backend_failure = (
                backend_fallback
                and record.get("ready") == 1
                and record.get("failure") == "none"
                and active_failure not in ("none", "unknown")
            )
            if (
                record.get(f"{active_prefix}Outcome") != 2
                or active_detail == 0
                or record.get(f"{inactive_prefix}Outcome") != 0
                or not (valid_domain_failure or valid_backend_failure)
            ):
                failures.append(
                    "classicFogBlendDomain named atomic fallback="
                    f"{record}"
                )
        for name in ("mismatches", "duplicate", "untracked"):
            if integer(active, name) != 0:
                failures.append(
                    f"classicFogBlendDomain {active_backend} {name}="
                    f"{active.get(name, 'missing')}"
                )
        for name in backend_counts:
            if integer(inactive, name) != 0:
                failures.append(
                    f"classicFogBlendDomain inactive {inactive_backend} {name}="
                    f"{inactive.get(name, 'missing')}"
                )
        return failures

    for name in ("requested", "prepared", "frameValid"):
        if integer(fields, name) != 1:
            failures.append(
                f"classicFogBlendDomain {name}={fields.get(name, 'missing')}"
            )
    if integer(fields, "overflow") != 0 or integer(fields, "fallback") != 0:
        failures.append(
            "classicFogBlendDomain rollback accounting="
            f"{fields.get('overflow', 'missing')}/{fields.get('fallback', 'missing')}"
        )
    if views is None or views <= 0 or integer(fields, "ready") != views:
        failures.append(
            "classicFogBlendDomain complete-view readiness="
            f"{fields.get('ready', 'missing')}/{fields.get('views', 'missing')}"
        )
    positive_counts = [
        "lights",
        "fog",
        "blend",
        "surfaces",
        "stages",
        "active",
        "primitives",
        "fogReceivers",
        "fogCaps",
        "textures",
    ]
    if expectation != "owned-skip-blend":
        positive_counts.append("blendDraws")
    for name in positive_counts:
        value = integer(fields, name)
        if value is None or value <= 0:
            failures.append(
                f"classicFogBlendDomain {name}={fields.get(name, 'missing')}"
            )
    if expectation == "owned-skip-blend":
        if integer(fields, "blendDraws") != 0:
            failures.append(
                "classicFogBlendDomain skip-blend draw accounting="
                f"{fields.get('blendDraws', 'missing')}"
            )
        noop_lights = integer(fields, "noopLights")
        if noop_lights is None or noop_lights <= 0:
            failures.append(
                "classicFogBlendDomain skip-blend light accounting="
                f"{fields.get('noopLights', 'missing')}"
            )
    if integer(fields, "lights") != (
        (integer(fields, "fog") or 0) + (integer(fields, "blend") or 0)
    ):
        failures.append("classicFogBlendDomain light-class accounting")
    if integer(fields, "surfaces") != (
        (integer(fields, "global") or 0) + (integer(fields, "local") or 0)
    ):
        failures.append("classicFogBlendDomain receiver-class accounting")
    if fields.get("status") != "ready":
        failures.append(
            f"classicFogBlendDomain status={fields.get('status', 'missing')}"
        )

    if integer(active, "ownedViews") != views or integer(active, "fallbackViews") != 0:
        failures.append(
            f"classicFogBlendDomain {active_backend} view coverage"
        )
    for backend_name, aggregate_name in (
        ("ownedFogReceivers", "fogReceivers"),
        ("ownedFogCaps", "fogCaps"),
        ("ownedBlend", "blendDraws"),
        ("ownedNoops", "noop"),
        ("ownedNoopStages", "noopStages"),
        ("ownedNoopLights", "noopLights"),
    ):
        if integer(active, backend_name) != integer(fields, aggregate_name):
            failures.append(
                f"classicFogBlendDomain {active_backend} {backend_name} accounting"
            )
    for name in ("mismatches", "duplicate", "untracked"):
        if integer(active, name) != 0:
            failures.append(
                f"classicFogBlendDomain {active_backend} {name}="
                f"{active.get(name, 'missing')}"
            )
    for name in backend_counts:
        if integer(inactive, name) != 0:
            failures.append(
                f"classicFogBlendDomain inactive {inactive_backend} {name}="
                f"{inactive.get(name, 'missing')}"
            )
    return failures


def load_tga_rgb(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if len(data) < 18:
        raise ValueError("file is too small to be a TGA")
    id_length, color_map_type, image_type = data[0], data[1], data[2]
    if color_map_type != 0 or image_type not in (2, 3):
        raise ValueError(f"unsupported TGA type {image_type} with color map {color_map_type}")
    width = struct.unpack_from("<H", data, 12)[0]
    height = struct.unpack_from("<H", data, 14)[0]
    bits = data[16]
    if width <= 0 or height <= 0 or bits not in (24, 32):
        raise ValueError(f"unsupported TGA dimensions/depth {width}x{height}x{bits}")
    pixel_size = bits // 8
    pixel_count = width * height
    start = 18 + id_length
    end = start + pixel_count * pixel_size
    if len(data) < end:
        raise ValueError("truncated TGA pixel payload")
    pixels = data[start:end]
    rgb = bytearray(pixel_count * 3)
    for i in range(pixel_count):
        src = i * pixel_size
        dst = i * 3
        if image_type == 3:
            value = pixels[src]
            rgb[dst : dst + 3] = bytes((value, value, value))
        else:
            b, g, r = pixels[src], pixels[src + 1], pixels[src + 2]
            rgb[dst : dst + 3] = bytes((r, g, b))
    return width, height, bytes(rgb)


def compare_tga(actual: Path, reference: Path) -> dict[str, Any]:
    aw, ah, ap = load_tga_rgb(actual)
    rw, rh, rp = load_tga_rgb(reference)
    if (aw, ah) != (rw, rh):
        return {
            "status": "dimension-mismatch",
            "actualSize": f"{aw}x{ah}",
            "referenceSize": f"{rw}x{rh}",
        }
    total_sq = 0
    max_delta = 0
    differing = 0
    for a, r in zip(ap, rp):
        delta = abs(a - r)
        if delta:
            differing += 1
            total_sq += delta * delta
            max_delta = max(max_delta, delta)
    rms = math.sqrt(total_sq / max(1, len(ap)))
    return {
        "status": "compared",
        "actualSize": f"{aw}x{ah}",
        "referenceSize": f"{rw}x{rh}",
        "rms": round(rms, 4),
        "maxDelta": max_delta,
        "differingChannels": differing,
    }


def evaluate_display_evidence(
    sources: Iterable[str],
    screenshot: Path | None,
    expected_width: int = BUDGET_WIDTH,
    expected_height: int = BUDGET_HEIGHT,
    expected_presentation: str = "windowed",
) -> tuple[dict[str, Any], list[str]]:
    """Bind the requested display to the renderer's actual mode and TGA dimensions."""
    expected = {
        "modeSelector": "-1",
        "width": expected_width,
        "height": expected_height,
        "presentation": expected_presentation,
    }
    evidence: dict[str, Any] = {
        "expected": expected,
        "runtime": None,
        "screenshot": None,
    }
    failures: list[str] = []
    runtime_values = {
        (selector.strip(), int(width), int(height), presentation.casefold())
        for source in sources
        for selector, width, height, presentation in RUNTIME_DISPLAY_MODE_PATTERN.findall(source)
    }
    if not runtime_values:
        failures.append("runtime MODE evidence is missing")
    elif len(runtime_values) != 1:
        failures.append("runtime MODE evidence is conflicting")
    else:
        selector, width, height, presentation = next(iter(runtime_values))
        runtime = {
            "modeSelector": selector,
            "width": width,
            "height": height,
            "presentation": presentation,
        }
        evidence["runtime"] = runtime
        if runtime != expected:
            failures.append(
                "runtime MODE is "
                f"{selector}, {width}x{height} {presentation}; expected -1, "
                f"{expected_width}x{expected_height} {expected_presentation}"
            )

    if screenshot is None:
        failures.append("engine screenshot is missing for display verification")
    else:
        try:
            width, height, _ = load_tga_rgb(screenshot)
        except (OSError, ValueError) as exc:
            failures.append(f"engine screenshot is not a valid TGA: {exc}")
        else:
            screenshot_evidence = {"width": width, "height": height}
            evidence["screenshot"] = screenshot_evidence
            if screenshot_evidence != {
                "width": expected_width,
                "height": expected_height,
            }:
                failures.append(
                    f"engine screenshot is {width}x{height}; expected "
                    f"{expected_width}x{expected_height}"
                )
    return evidence, failures


def screenshot_reference_candidates(
    reference_dir: Path, screenshot: Path, savepath: Path, case_id: str | None = None
) -> list[Path]:
    # Case-scoped candidates take precedence: every case captures the same
    # relative screenshot name (screenshots/renderer-bench/sp_0.tga), so a
    # flat reference directory can only ever serve a single case per profile.
    candidates = [reference_dir / screenshot.name]
    if case_id:
        candidates.insert(0, reference_dir / case_id / screenshot.name)
    for game_dir in ("baseoq4", "q4base"):
        root = savepath / game_dir
        try:
            rel = screenshot.relative_to(root)
            candidates.insert(0, reference_dir / rel)
            if case_id:
                candidates.insert(0, reference_dir / case_id / rel)
                # A reference tree produced by this runner retains both the
                # case savepath and game-directory components:
                # savepaths/<case>/baseoq4/screenshots/...
                candidates.insert(0, reference_dir / case_id / game_dir / rel)
        except ValueError:
            pass
    return candidates


def compare_screenshot_if_requested(
    screenshot: Path | None,
    savepath: Path,
    reference_dir: Path | None,
    rms_threshold: float,
    max_threshold: int,
    require_reference: bool,
    case_id: str | None = None,
) -> dict[str, Any]:
    if screenshot is None:
        return {"status": "missing-screenshot"}
    result: dict[str, Any] = {
        "status": "not-requested",
        "actual": str(screenshot),
        "sha256": hashlib.sha256(screenshot.read_bytes()).hexdigest(),
    }
    if reference_dir is None:
        return result
    for candidate in screenshot_reference_candidates(reference_dir, screenshot, savepath, case_id):
        if candidate.exists():
            comparison = compare_tga(screenshot, candidate)
            comparison["actual"] = str(screenshot)
            comparison["reference"] = str(candidate)
            if comparison["status"] == "compared":
                comparison["pass"] = comparison["rms"] <= rms_threshold and comparison["maxDelta"] <= max_threshold
            return comparison
    result["status"] = "missing-reference" if require_reference else "reference-not-found"
    result["referenceDir"] = str(reference_dir)
    result["pass"] = not require_reference
    return result


def compare_screenshot_difference_if_requested(
    screenshot: Path | None,
    savepath: Path,
    reference_dir: Path | None,
    min_rms: float,
    min_differing_channels: int,
    case_id: str | None = None,
) -> dict[str, Any]:
    """Require a material image delta from a second engine-TGA reference.

    Equivalence and effectiveness are separate claims.  The ordinary reference
    comparison proves shared-off/shared-on parity under identical renderer
    settings; this comparison proves that a controlled feature-on scene is
    actually different from its feature-disabled capture.  Eligible features
    include interaction shadows, fog/blend, and material deforms.
    """
    if screenshot is None:
        return {"status": "missing-screenshot", "pass": False}
    if reference_dir is None:
        return {"status": "not-requested"}
    for candidate in screenshot_reference_candidates(
        reference_dir, screenshot, savepath, case_id
    ):
        if not candidate.exists():
            continue
        comparison = compare_tga(screenshot, candidate)
        comparison["actual"] = str(screenshot)
        comparison["reference"] = str(candidate)
        comparison["minimumRms"] = min_rms
        comparison["minimumDifferingChannels"] = min_differing_channels
        if comparison["status"] == "compared":
            comparison["status"] = "difference-compared"
            comparison["pass"] = (
                comparison["rms"] >= min_rms
                and comparison["differingChannels"] >= min_differing_channels
            )
        else:
            comparison["pass"] = False
        return comparison
    return {
        "status": "missing-difference-reference",
        "referenceDir": str(reference_dir),
        "pass": False,
    }


def evaluate_scene_view_evidence(
    sources: Iterable[tuple[str, str]], contract: dict[str, Any] | None
) -> tuple[dict[str, Any], list[str]]:
    """Check retained endpoint poses, not continuous motion or whole-scene parity."""
    if contract is None:
        return {"required": False, "status": "not-required"}, []
    failures: list[str] = []
    observations: list[dict[str, Any]] = []
    state_pattern = re.compile(
        r"openQ4 map state: map=(\S*) entityFilter=(\S*) "
        r"expectedMap=(\S*) expectedEntityFilter=(\S*)"
    )
    pose_pattern = re.compile(r"origin: \(([^()]+)\) angles: \(([^()]+)\)")
    for source_name, source_text in sources:
        lines = source_text.splitlines()
        endpoints = [(i, line.strip()) for i, line in enumerate(lines)
                     if line.strip() in (VIEW_BEGIN, VIEW_END)]
        if not endpoints and source_name != "log":
            continue
        if [marker for _, marker in endpoints] != [VIEW_BEGIN, VIEW_END]:
            failures.append(f"{source_name}: missing, repeated or reordered scene endpoints")
            continue
        snapshots: list[dict[str, Any]] = []
        for index, marker in endpoints:
            state = state_pattern.fullmatch(lines[index + 1].strip()) if index + 1 < len(lines) else None
            pose = pose_pattern.fullmatch(lines[index + 2].strip()) if index + 2 < len(lines) else None
            if state is None or pose is None:
                failures.append(f"{source_name}: {marker} lacks map/filter and camera evidence")
                continue
            try:
                origin, angles = ([float(value) for value in group.split()] for group in pose.groups())
                if len(origin) != 3 or len(angles) != 3 or not all(map(math.isfinite, origin + angles)):
                    raise ValueError("invalid pose")
            except ValueError:
                failures.append(f"{source_name}: {marker} has an invalid camera pose")
                continue
            expected_state = (contract["map"], contract["entityFilter"]) * 2
            if state.groups() != expected_state:
                failures.append(f"{source_name}: {marker} map/entity filter differs from the scene")
            snapshots.append({
                "map": state.group(1), "entityFilter": state.group(2),
                "expectedMap": state.group(3), "expectedEntityFilter": state.group(4),
                "origin": origin, "angles": angles,
            })
        if len(snapshots) != 2:
            continue
        begin, end = snapshots
        angle_distance = lambda a, b: max(abs((x - y + 180.0) % 360.0 - 180.0) for x, y in zip(a, b))
        position_delta = math.dist(begin["origin"], end["origin"])
        angle_delta = angle_distance(begin["angles"], end["angles"])
        observation = {"source": source_name, "begin": begin, "end": end,
                       "positionDelta": round(position_delta, 6), "angleDelta": round(angle_delta, 6)}
        observations.append(observation)
        if contract["viewPolicy"] == "static-endpoints":
            if position_delta > contract["maxPositionDelta"] or angle_delta > contract["maxAngleDelta"]:
                failures.append(f"{source_name}: static camera moved between sampling endpoints")
            for snapshot in snapshots:
                if (math.dist(snapshot["origin"], contract["expectedOrigin"]) > contract["maxPositionDelta"]
                        or angle_distance(snapshot["angles"], contract["expectedAngles"]) > contract["maxAngleDelta"]):
                    failures.append(f"{source_name}: camera differs from the settled stock gameplay view")
                    break
    log_observations = [item for item in observations if item["source"] == "log"]
    if len(log_observations) != 1:
        failures.append("engine log does not prove both scene endpoints")
    elif any(item[endpoint] != log_observations[0][endpoint]
             for item in observations for endpoint in ("begin", "end")):
        failures.append("scene endpoint evidence disagrees between diagnostic streams")
    return {"required": True, "status": "fail" if failures else "pass",
            "contract": contract, "observations": observations}, failures


def evaluate_role_result(
    spec: RunSpec,
    role: str,
    exit_code: int,
    timed_out: bool,
    elapsed_seconds: float,
    savepath: Path,
    log_name: str,
    stdout_path: Path,
    stderr_path: Path,
    screenshot_rel: str,
    reference_dir: Path | None,
    rms_threshold: float,
    max_threshold: int,
    require_reference: bool,
    require_benchmark: bool = True,
    min_pacing_hz: float = 0.0,
    max_p95_ms: float = 0.0,
    max_p99_ms: float = 0.0,
    budget_contract: dict[str, Any] | None = None,
    budget_profile: str = "baseline",
    difference_reference_dir: Path | None = None,
    difference_min_rms: float = 0.1,
    difference_min_channels: int = 1000,
    pbr_fixture_acceptance_required: bool = False,
    autoexec_path: Path | None = None,
    display_width: int = BUDGET_WIDTH,
    display_height: int = BUDGET_HEIGHT,
    max_frame_ms: float = 0.0,
) -> dict[str, Any]:
    log_path = find_log(savepath, log_name)
    diagnostic_sources = (
        ("log", read_text(log_path)),
        ("stdout", read_text(stdout_path)),
        ("stderr", read_text(stderr_path)),
    )
    text = "\n".join(part for _, part in diagnostic_sources if part)
    screenshot = find_screenshot(savepath, screenshot_rel)
    warnings = warning_counts(text)
    failure_diagnostics, failure_diagnostics_omitted = collect_failure_diagnostics(diagnostic_sources)
    summary = extract_summary(text)
    pbr_fixture_evidence, pbr_fixture_failures = evaluate_pbr_fixture_evidence(
        summary, pbr_fixture_acceptance_required, spec.render_api
    )
    map_evidence, map_evidence_failures = evaluate_controlled_map_evidence(
        diagnostic_sources, pbr_fixture_acceptance_required
    )
    scene_evidence, scene_failures = evaluate_scene_view_evidence(diagnostic_sources, spec.scene_contract)
    image = compare_screenshot_if_requested(
        screenshot,
        savepath,
        reference_dir,
        rms_threshold,
        max_threshold,
        require_reference,
        spec.id,
    )
    is_deform_scene = spec.case_id in DEFORM_SCENES
    difference_feature_case = (
        is_deform_scene
        or spec.fog_blend_expectation in ("owned", "owned-skip-blend")
        or spec.interaction_shadow_expectation
        in (
            "stencil",
            "projected",
            "point",
            "mapped",
            "csm",
            "mixed",
            "dynamic",
            "perforated",
            "hybrid",
        )
    )
    difference_required = (
        difference_reference_dir is not None and difference_feature_case
    )
    difference_reference_case_id = (
        spec.id
        if (
            is_deform_scene
            or spec.fog_blend_expectation in ("owned", "owned-skip-blend")
        )
        else spec.id_for_shadow_preset("unshadowed")
    )
    image_difference = (
        compare_screenshot_difference_if_requested(
            screenshot,
            savepath,
            difference_reference_dir,
            difference_min_rms,
            difference_min_channels,
            difference_reference_case_id,
        )
        if difference_required
        else {"status": "not-requested"}
    )
    missing: list[str] = []
    budget_evidence: dict[str, Any] = {}
    display_evidence: dict[str, Any] = {}
    if timed_out:
        missing.append("timeout")
    if log_path is None:
        missing.append("log file")
    if require_benchmark and "rendererBenchmark capture(" not in text:
        missing.append("renderer benchmark capture line")
    if require_benchmark and "Renderer benchmark:" not in text:
        missing.append("gfxInfo benchmark line")
    threshold_pass = summary.get("thresholdPass")
    if require_benchmark and threshold_pass != "1":
        missing.append(f"renderer benchmark thresholdPass={threshold_pass or 'missing'}")
    elif threshold_pass is not None and threshold_pass != "1":
        missing.append(f"renderer benchmark thresholdPass={threshold_pass}")
    if min_pacing_hz > 0.0:
        pacing_hz = summary_float(summary, "pacingHz")
        if pacing_hz is None:
            missing.append("frame pacing Hz")
        elif pacing_hz < min_pacing_hz:
            missing.append(f"pacingHz={pacing_hz:.1f}<{min_pacing_hz:.1f}")
    if max_p95_ms > 0.0:
        p95_ms = summary_float(summary, "pacingP95Ms")
        if p95_ms is None:
            missing.append("frame pacing p95")
        elif p95_ms > max_p95_ms:
            missing.append(f"pacingP95={p95_ms:.1f}>{max_p95_ms:.1f}")
    if max_p99_ms > 0.0:
        p99_ms = summary_float(summary, "pacingP99Ms")
        if p99_ms is None:
            missing.append("frame pacing p99")
        elif p99_ms > max_p99_ms:
            missing.append(f"pacingP99={p99_ms:.1f}>{max_p99_ms:.1f}")
    if max_frame_ms > 0.0:
        maximum = summary_float(summary, "pacingMaxMs")
        if maximum is None:
            missing.append("frame pacing maximum")
        elif maximum > max_frame_ms:
            missing.append(f"pacingMax={maximum:.2f}>{max_frame_ms:.2f}")
    missing.extend(evaluate_shared_interaction_evidence(spec, summary))
    missing.extend(evaluate_shared_fog_blend_evidence(spec, summary))
    missing.extend(pbr_fixture_failures)
    missing.extend(map_evidence_failures)
    missing.extend(f"scene evidence: {failure}" for failure in scene_failures)
    if spec.render_api == "gl" and "Selected renderer tier:" not in text:
        missing.append("selected tier line")
    if pbr_fixture_acceptance_required and not require_benchmark:
        if (
            spec.case_id != PBR_FIXTURE_BENCHMARK_CASE
            or spec.mode != "SP"
            or normalize_engine_map_name(spec.map_name)
            != normalize_engine_map_name(PBR_FIXTURE_STOCK_MAP)
        ):
            missing.append("controlled fixture case/mode/map identity")
        if spec.render_api == "gl":
            selected_tiers: set[str] = set()
            for _, source_text in diagnostic_sources:
                tier_matches = list(
                    re.finditer(
                        r"^Selected renderer tier:\s*([A-Za-z0-9]+)\s*$",
                        source_text,
                        re.IGNORECASE | re.MULTILINE,
                    )
                )
                if tier_matches:
                    selected_tiers.add(tier_matches[-1].group(1).casefold())
            expected_runtime_tier = GL_TIER_RUNTIME_NAMES.get(spec.tier)
            if (
                expected_runtime_tier is None
                or selected_tiers != {expected_runtime_tier.casefold()}
            ):
                missing.append(
                    "controlled GL fixture selected tier must match the requested tier"
                )
    if screenshot is None:
        missing.append("screenshot")
    if any(count > 0 for count in warnings.values()):
        missing += [f"{name}={count}" for name, count in warnings.items() if count > 0]
    if image.get("pass") is False:
        missing.append(f"image comparison {image.get('status')}")
    if image_difference.get("pass") is False:
        missing.append(
            "image difference "
            f"{image_difference.get('status')} rms="
            f"{image_difference.get('rms', 'missing')} channels="
            f"{image_difference.get('differingChannels', 'missing')}"
        )
    display_evidence, display_failures = evaluate_display_evidence(
        (item[1] for item in diagnostic_sources), screenshot,
        BUDGET_WIDTH if require_benchmark else display_width,
        BUDGET_HEIGHT if require_benchmark else display_height,
        "windowed" if require_benchmark else spec.display_mode,
    )
    missing.extend(f"display evidence: {failure}" for failure in display_failures)
    if require_benchmark and budget_contract is not None:
        budget_evidence, budget_failures = evaluate_timing_evidence(
            (item[1] for item in diagnostic_sources),
            budget_contract,
            spec.budget_map_name,
            spec.expected_backend,
            budget_profile,
        )
        missing.extend(f"renderer budget: {failure}" for failure in budget_failures)

    ok = exit_code == 0 and not timed_out and not missing
    return {
        "id": spec.id,
        "role": role,
        "status": "pass" if ok else "fail",
        "exitCode": exit_code,
        "timedOut": timed_out,
        "elapsedSeconds": round(elapsed_seconds, 2),
        "log": str(log_path) if log_path is not None else "",
        "stdout": str(stdout_path),
        "stderr": str(stderr_path),
        "autoexecCfg": str(autoexec_path) if autoexec_path is not None else "",
        "screenshot": str(screenshot) if screenshot is not None else "",
        "screenshotRequest": screenshot_rel,
        "warnings": warnings,
        "failureDiagnostics": failure_diagnostics,
        "failureDiagnosticsOmitted": failure_diagnostics_omitted,
        "missing": missing,
        "summary": summary,
        "image": image,
        "imageDifference": image_difference,
        "displayEvidence": display_evidence,
        "budgetEvidence": budget_evidence,
        "pbrFixtureEvidence": pbr_fixture_evidence,
        "mapEvidence": map_evidence,
        "sceneEvidence": scene_evidence,
    }


def launch_and_wait(
    executable: Path,
    args: list[str],
    cwd: Path,
    stdout_path: Path,
    stderr_path: Path,
    timeout_seconds: int,
) -> tuple[int, bool, float]:
    started = time.time()
    timed_out = False
    with stdout_path.open("w", encoding="utf-8", errors="replace") as stdout_file, stderr_path.open("w", encoding="utf-8", errors="replace") as stderr_file:
        process = subprocess.Popen(
            [str(executable)] + args,
            cwd=str(cwd),
            stdout=stdout_file,
            stderr=stderr_file,
        )
        try:
            exit_code = process.wait(timeout=timeout_seconds)
        except subprocess.TimeoutExpired:
            timed_out = True
            process.kill()
            exit_code = process.wait(timeout=10)
    elapsed = time.time() - started
    return exit_code, timed_out, elapsed


def run_sp_spec(
    root: Path,
    executable: Path,
    output_dir: Path,
    basepath: str,
    run_id: str,
    spec: RunSpec,
    args: argparse.Namespace,
) -> dict[str, Any]:
    pbr_fixture_acceptance_required = requires_pbr_fixture_acceptance(
        args.extra_cvars, spec.render_api
    )
    savepath = output_dir / "savepaths" / spec.id
    savepath.mkdir(parents=True, exist_ok=True)
    log_name = ROLE_LOG_NAME
    log_path = find_log(savepath, log_name)
    if log_path is not None:
        log_path.unlink()
    stdout_path = output_dir / f"{spec.id}.out.txt"
    stderr_path = output_dir / f"{spec.id}.err.txt"
    autoexec_cfg, screenshot_rel = write_autoexec_cfg(
        savepath,
        spec,
        "sp",
        run_id,
        args.settle_frames,
        args.sample_frames,
        args.sample_msec,
        args.extra_cvars,
        args.exec_commands,
        args.gpu_timers,
        not args.pacing_only,
    )
    game_args = common_args(
        root,
        args.runtime_dir_path,
        savepath,
        log_name,
        basepath,
        spec,
        args.width,
        args.height,
        args.benchmark_preset,
        args.modern_executor,
        args.show_fps_overlay,
        args.launch_cvars,
        autoexec_cfg,
        args.autoexec_delay_ms,
    )
    append_set(game_args, "si_gameType", "singleplayer")
    append_command(game_args, "map", spec.map_name)
    if spec.scene_contract:
        game_args.append(spec.scene_contract["entityFilter"])

    if args.dry_run:
        return {
            "id": spec.id,
            "mode": spec.mode,
            "map": spec.map_name,
            "caseId": spec.case_id,
            "sceneContract": spec.scene_contract,
            "budgetMap": spec.budget_map_name,
            "expectedBackend": spec.expected_backend,
            "renderApi": spec.render_api,
            "interactionExpectation": spec.interaction_expectation,
            "interactionShadowExpectation": spec.interaction_shadow_expectation,
            "fogBlendExpectation": spec.fog_blend_expectation,
            "pbrFixtureAcceptanceRequired": pbr_fixture_acceptance_required,
            "displayContract": display_launch_contract(spec, args.width, args.height),
            "status": "planned",
            "args": game_args,
            "autoexecCfg": autoexec_cfg,
            "screenshotRequest": screenshot_rel,
            "roles": [],
        }

    exit_code, timed_out, elapsed = launch_and_wait(
        executable,
        game_args,
        args.runtime_dir_path,
        stdout_path,
        stderr_path,
        args.timeout,
    )
    role_result = evaluate_role_result(
        spec,
        "sp",
        exit_code,
        timed_out,
        elapsed,
        savepath,
        log_name,
        stdout_path,
        stderr_path,
        screenshot_rel,
        args.reference_dir_path,
        args.image_rms_threshold,
        args.image_max_threshold,
        args.require_references,
        require_benchmark=not args.pacing_only,
        min_pacing_hz=args.min_pacing_hz,
        max_p95_ms=args.max_p95_ms,
        max_p99_ms=args.max_p99_ms,
        budget_contract=args.budget_contract,
        budget_profile=args.benchmark_preset,
        difference_reference_dir=args.difference_reference_dir_path,
        difference_min_rms=args.image_difference_min_rms,
        difference_min_channels=args.image_difference_min_channels,
        pbr_fixture_acceptance_required=pbr_fixture_acceptance_required,
        autoexec_path=savepath / "baseoq4" / Path(autoexec_cfg),
        display_width=args.width, display_height=args.height,
        max_frame_ms=args.max_frame_ms,
    )
    return {
        "id": spec.id,
        "mode": spec.mode,
        "map": spec.map_name,
        "caseId": spec.case_id,
        "sceneContract": spec.scene_contract,
        "budgetMap": spec.budget_map_name,
        "expectedBackend": spec.expected_backend,
        "renderApi": spec.render_api,
        "interactionExpectation": spec.interaction_expectation,
        "interactionShadowExpectation": spec.interaction_shadow_expectation,
        "fogBlendExpectation": spec.fog_blend_expectation,
        "pbrFixtureAcceptanceRequired": pbr_fixture_acceptance_required,
        "displayContract": display_launch_contract(spec, args.width, args.height),
        "purpose": spec.purpose,
        "tier": spec.tier,
        "maxfps": spec.maxfps,
        "swapInterval": spec.swap_interval,
        "display": spec.display_mode,
        "shadowPreset": spec.shadow_preset,
        "renderer": spec.renderer,
        "status": role_result["status"],
        "roles": [role_result],
    }


def run_mp_spec(
    root: Path,
    executable: Path,
    output_dir: Path,
    basepath: str,
    run_id: str,
    spec: RunSpec,
    index: int,
    args: argparse.Namespace,
) -> dict[str, Any]:
    pbr_fixture_acceptance_required = requires_pbr_fixture_acceptance(
        args.extra_cvars, spec.render_api
    )
    port = args.mp_port + index
    server_savepath = output_dir / "savepaths" / f"{spec.id}_server"
    client_savepath = output_dir / "savepaths" / f"{spec.id}_client"
    server_savepath.mkdir(parents=True, exist_ok=True)
    client_savepath.mkdir(parents=True, exist_ok=True)

    server_log = ROLE_LOG_NAME
    client_log = ROLE_LOG_NAME
    for savepath, log_name in ((server_savepath, server_log), (client_savepath, client_log)):
        log_path = find_log(savepath, log_name)
        if log_path is not None:
            log_path.unlink()

    server_stdout = output_dir / f"{spec.id}_server.out.txt"
    server_stderr = output_dir / f"{spec.id}_server.err.txt"
    client_stdout = output_dir / f"{spec.id}_client.out.txt"
    client_stderr = output_dir / f"{spec.id}_client.err.txt"

    server_settle_frames = args.settle_frames + args.mp_client_delay_frames
    # Keep the listen server alive while a fresh client initializes and builds
    # its local caches. A frame-count grace collapses to only a few seconds at
    # the 240 FPS budget workload and can let the server quit mid-connect.
    server_exec_commands = args.exec_commands + (
        f"waitMsec {MP_SERVER_CLIENT_GRACE_MSEC}",
    )
    server_autoexec_cfg, server_screenshot = write_autoexec_cfg(
        server_savepath,
        spec,
        "server",
        run_id,
        server_settle_frames,
        args.sample_frames,
        args.sample_msec,
        args.extra_cvars,
        server_exec_commands,
        args.gpu_timers,
        not args.pacing_only,
    )
    server_args = common_args(
        root,
        args.runtime_dir_path,
        server_savepath,
        server_log,
        basepath,
        spec,
        args.width,
        args.height,
        args.benchmark_preset,
        args.modern_executor,
        args.show_fps_overlay,
        args.launch_cvars,
        server_autoexec_cfg,
        args.autoexec_delay_ms,
    )
    append_set(server_args, "net_serverDedicated", "0")
    append_set(server_args, "net_port", str(port))
    append_set(server_args, "ui_autoJoin", "1")
    server_args += ["+seta", "si_pure", "1"]
    append_set(server_args, "net_serverAllowServerMod", "0")
    append_set(server_args, "sv_cheats", "1")
    append_set(server_args, "si_gameType", "DM")
    append_command(server_args, "spawnServer", spec.map_name)

    client_capture_index = 1 if spec.path_name == "postinit-connect" else 0
    client_autoexec_cfg, client_screenshot = write_autoexec_cfg(
        client_savepath,
        spec,
        "client",
        run_id,
        args.settle_frames,
        args.sample_frames,
        args.sample_msec,
        args.extra_cvars,
        args.exec_commands,
        args.gpu_timers,
        not args.pacing_only,
        client_capture_index,
    )
    client_reconnect_cfg = ""
    client_initial_autoexec_cfg = client_autoexec_cfg
    if spec.path_name == "postinit-connect":
        client_reconnect_cfg = write_postinit_reconnect_cfg(
            client_savepath,
            client_autoexec_cfg,
            args.autoexec_delay_ms,
        )
        client_initial_autoexec_cfg = client_reconnect_cfg
    client_args = common_args(
        root,
        args.runtime_dir_path,
        client_savepath,
        client_log,
        basepath,
        spec,
        args.width,
        args.height,
        args.benchmark_preset,
        args.modern_executor,
        args.show_fps_overlay,
        args.launch_cvars,
        client_initial_autoexec_cfg,
        args.autoexec_delay_ms,
    )
    append_set(client_args, "ui_autoJoin", "1")
    append_set(client_args, "ui_name", "RendererBenchClient")
    client_postinit_connect_cfg = ""
    if spec.path_name == "postinit-connect":
        append_set(client_args, "si_gameType", "singleplayer")
        client_postinit_connect_cfg = write_postinit_connect_cfg(client_savepath, port)
        append_command(client_args, "exec", client_postinit_connect_cfg)
    else:
        append_command(client_args, "connect", f"127.0.0.1:{port}")

    if args.dry_run:
        return {
            "id": spec.id,
            "mode": spec.mode,
            "map": spec.map_name,
            "caseId": spec.case_id,
            "sceneContract": spec.scene_contract,
            "budgetMap": spec.budget_map_name,
            "expectedBackend": spec.expected_backend,
            "renderApi": spec.render_api,
            "interactionExpectation": spec.interaction_expectation,
            "interactionShadowExpectation": spec.interaction_shadow_expectation,
            "fogBlendExpectation": spec.fog_blend_expectation,
            "pbrFixtureAcceptanceRequired": pbr_fixture_acceptance_required,
            "displayContract": display_launch_contract(spec, args.width, args.height),
            "status": "planned",
            "serverArgs": server_args,
            "clientArgs": client_args,
            "serverAutoexecCfg": server_autoexec_cfg,
            "clientAutoexecCfg": client_autoexec_cfg,
            "clientInitialAutoexecCfg": client_initial_autoexec_cfg,
            "clientReconnectCfg": client_reconnect_cfg,
            "clientPostInitConnectCfg": client_postinit_connect_cfg,
            "serverScreenshotRequest": server_screenshot,
            "clientScreenshotRequest": client_screenshot,
            "roles": [],
        }

    started = time.time()
    server_timed_out = False
    client_timed_out = False
    with server_stdout.open("w", encoding="utf-8", errors="replace") as server_out, server_stderr.open("w", encoding="utf-8", errors="replace") as server_err:
        server_process = subprocess.Popen(
            [str(executable)] + server_args,
            cwd=str(args.runtime_dir_path),
            stdout=server_out,
            stderr=server_err,
        )
    time.sleep(max(1, args.mp_client_delay))
    with client_stdout.open("w", encoding="utf-8", errors="replace") as client_out, client_stderr.open("w", encoding="utf-8", errors="replace") as client_err:
        client_process = subprocess.Popen(
            [str(executable)] + client_args,
            cwd=str(args.runtime_dir_path),
            stdout=client_out,
            stderr=client_err,
        )

    try:
        client_exit = client_process.wait(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        client_timed_out = True
        client_process.kill()
        client_exit = client_process.wait(timeout=10)

    remaining = max(10, args.timeout - int(time.time() - started))
    try:
        server_exit = server_process.wait(timeout=remaining)
    except subprocess.TimeoutExpired:
        server_timed_out = True
        server_process.kill()
        server_exit = server_process.wait(timeout=10)

    elapsed = time.time() - started
    server_result = evaluate_role_result(
        spec,
        "server",
        server_exit,
        server_timed_out,
        elapsed,
        server_savepath,
        server_log,
        server_stdout,
        server_stderr,
        server_screenshot,
        args.reference_dir_path,
        args.image_rms_threshold,
        args.image_max_threshold,
        args.require_references,
        require_benchmark=not args.pacing_only,
        min_pacing_hz=args.min_pacing_hz,
        max_p95_ms=args.max_p95_ms,
        max_p99_ms=args.max_p99_ms,
        budget_contract=args.budget_contract,
        budget_profile=args.benchmark_preset,
        difference_reference_dir=args.difference_reference_dir_path,
        difference_min_rms=args.image_difference_min_rms,
        difference_min_channels=args.image_difference_min_channels,
        pbr_fixture_acceptance_required=pbr_fixture_acceptance_required,
        autoexec_path=server_savepath / "baseoq4" / Path(server_autoexec_cfg),
        display_width=args.width, display_height=args.height,
        max_frame_ms=args.max_frame_ms,
    )
    client_result = evaluate_role_result(
        spec,
        "client",
        client_exit,
        client_timed_out,
        elapsed,
        client_savepath,
        client_log,
        client_stdout,
        client_stderr,
        client_screenshot,
        args.reference_dir_path,
        args.image_rms_threshold,
        args.image_max_threshold,
        args.require_references,
        require_benchmark=not args.pacing_only,
        min_pacing_hz=args.min_pacing_hz,
        max_p95_ms=args.max_p95_ms,
        max_p99_ms=args.max_p99_ms,
        budget_contract=args.budget_contract,
        budget_profile=args.benchmark_preset,
        difference_reference_dir=args.difference_reference_dir_path,
        difference_min_rms=args.image_difference_min_rms,
        difference_min_channels=args.image_difference_min_channels,
        pbr_fixture_acceptance_required=pbr_fixture_acceptance_required,
        autoexec_path=client_savepath / "baseoq4" / Path(client_autoexec_cfg),
        display_width=args.width, display_height=args.height,
        max_frame_ms=args.max_frame_ms,
    )
    postinit_connect_responses: dict[str, int] = {}
    postinit_ttf_rebuilds: dict[str, int] = {}
    if spec.path_name == "postinit-connect":
        server_log_text = read_text(find_log(server_savepath, server_log))
        client_log_text = read_text(find_log(client_savepath, client_log))
        server_text = server_log_text or "\n".join(
            read_text(path) for path in (server_stdout, server_stderr)
        )
        client_text = client_log_text or "\n".join(
            read_text(path) for path in (client_stdout, client_stderr)
        )
        server_response_count = server_text.count("sending connect response to ")
        client_response_count = client_text.count("received connect response from ")
        postinit_connect_responses = {
            "serverSent": server_response_count,
            "clientReceived": client_response_count,
        }
        client_ttf_rebuild_count = client_text.count("TTF font: console sheet rebuilt at ")
        postinit_ttf_rebuilds = {"clientAfterReload": client_ttf_rebuild_count}
        if server_response_count < 2:
            server_result["missing"].append(
                f"post-init connect/reconnect responses={server_response_count}<2"
            )
            server_result["status"] = "fail"
        if client_response_count < 2:
            client_result["missing"].append(
                f"post-init connect/reconnect responses={client_response_count}<2"
            )
            client_result["status"] = "fail"
        if client_ttf_rebuild_count < 1:
            client_result["missing"].append("post-reload TTF console atlas rebuild")
            client_result["status"] = "fail"
    ok = server_result["status"] == "pass" and client_result["status"] == "pass"
    return {
        "id": spec.id,
        "mode": spec.mode,
        "map": spec.map_name,
        "caseId": spec.case_id,
        "sceneContract": spec.scene_contract,
        "budgetMap": spec.budget_map_name,
        "expectedBackend": spec.expected_backend,
        "renderApi": spec.render_api,
        "interactionExpectation": spec.interaction_expectation,
        "interactionShadowExpectation": spec.interaction_shadow_expectation,
        "fogBlendExpectation": spec.fog_blend_expectation,
        "pbrFixtureAcceptanceRequired": pbr_fixture_acceptance_required,
        "displayContract": display_launch_contract(spec, args.width, args.height),
        "purpose": spec.purpose,
        "tier": spec.tier,
        "maxfps": spec.maxfps,
        "swapInterval": spec.swap_interval,
        "display": spec.display_mode,
        "shadowPreset": spec.shadow_preset,
        "renderer": spec.renderer,
        "status": "pass" if ok else "fail",
        "port": port,
        "postInitConnectResponses": postinit_connect_responses,
        "postInitTTFRebuilds": postinit_ttf_rebuilds,
        "roles": [server_result, client_result],
    }


def harness_failure_result(spec: RunSpec, exc: Exception) -> dict[str, Any]:
    message = f"harness exception: {type(exc).__name__}: {exc}"
    role = "client" if spec.mode == "MP" else "sp"
    role_result = {
        "id": spec.id,
        "role": role,
        "status": "fail",
        "exitCode": "",
        "timedOut": False,
        "elapsedSeconds": 0.0,
        "log": "",
        "stdout": "",
        "stderr": "",
        "screenshot": "",
        "screenshotRequest": "",
        "warnings": {},
        "failureDiagnostics": [],
        "failureDiagnosticsOmitted": 0,
        "missing": [message],
        "summary": {},
        "image": {"status": "harness-error"},
    }
    return {
        "id": spec.id,
        "mode": spec.mode,
        "map": spec.map_name,
        "caseId": spec.case_id,
        "sceneContract": spec.scene_contract,
        "budgetMap": spec.budget_map_name,
        "expectedBackend": spec.expected_backend,
        "renderApi": spec.render_api,
        "interactionExpectation": spec.interaction_expectation,
        "interactionShadowExpectation": spec.interaction_shadow_expectation,
        "fogBlendExpectation": spec.fog_blend_expectation,
        "purpose": spec.purpose,
        "tier": spec.tier,
        "maxfps": spec.maxfps,
        "swapInterval": spec.swap_interval,
        "display": spec.display_mode,
        "shadowPreset": spec.shadow_preset,
        "renderer": spec.renderer,
        "status": "fail",
        "roles": [role_result],
        "harnessError": message,
    }


def cvar_value_enabled(value: str) -> bool:
    # CVAR_BOOL canonicalization uses atoi() in the engine. Mirror its leading
    # integer semantics so values such as "0.0", "00", and "true" do not make
    # the harness expect ownership that the renderer will leave disabled.
    match = re.match(r"^\s*([+-]?\d+)", value)
    return match is not None and int(match.group(1)) != 0


def effective_interaction_cvars(
    args: argparse.Namespace, shadow_preset: str
) -> dict[str, str]:
    effective = {
        "r_renderersharedworldinteraction": "0",
        **{
            name.casefold(): value
            for name, value in SHADOW_PRESETS[shadow_preset].items()
        },
    }
    for name, value in args.extra_cvars:
        effective[name.casefold()] = value
    return effective


def interaction_expectation(
    args: argparse.Namespace, case_id: str, shadow_preset: str
) -> str:
    scene = ALL_SCENES.get(case_id, {})
    if case_id not in INTERACTION_SCENES and "interactionShadowTarget" not in scene:
        return "none"
    effective = effective_interaction_cvars(args, shadow_preset)
    if not cvar_value_enabled(
        effective.get("r_renderersharedworldinteraction", "0")
    ):
        return "disabled"
    shadows_enabled = cvar_value_enabled(effective.get("r_shadows", "0"))
    maps_enabled = shadows_enabled and cvar_value_enabled(
        effective.get("r_useshadowmap", "0")
    )
    if shadow_preset == "map-budget-fallback" and maps_enabled:
        return "fallback"
    # Debug overlays and translucent moment-map caster updates intentionally
    # stay outside the shared fixed-classic receiver corridor. Ordinary
    # stencil, projected/CSM, point-cube, and mixed map/stencil presets are
    # expected to retain whole-view shared ownership once their shadow state
    # has settled.
    if (
        maps_enabled
        and (
            cvar_value_enabled(effective.get("r_shadowmapdebugoverlay", "0"))
            or cvar_value_enabled(
                effective.get("r_shadowmaptranslucentmoments", "0")
            )
        )
    ):
        return "fallback"
    if (
        scene.get("interactionShadowTarget") == "fallback"
        and maps_enabled
    ):
        return "fallback"
    return "owned"


def interaction_shadow_expectation(
    args: argparse.Namespace, case_id: str, shadow_preset: str
) -> str:
    scene = ALL_SCENES.get(case_id, {})
    if case_id not in INTERACTION_SCENES and "interactionShadowTarget" not in scene:
        return "none"
    effective = effective_interaction_cvars(args, shadow_preset)
    if (
        not cvar_value_enabled(
            effective.get("r_renderersharedworldinteraction", "0")
        )
        or not cvar_value_enabled(effective.get("r_shadows", "0"))
    ):
        return "none"
    maps_enabled = cvar_value_enabled(effective.get("r_useshadowmap", "0"))
    if shadow_preset == "map-budget-fallback" and maps_enabled:
        return "map-budget-fallback"
    target = scene.get("interactionShadowTarget")
    if target == "fallback" and maps_enabled:
        return "translucent-fallback"
    if (
        maps_enabled
        and (
            cvar_value_enabled(effective.get("r_shadowmapdebugoverlay", "0"))
            or cvar_value_enabled(
                effective.get("r_shadowmaptranslucentmoments", "0")
            )
        )
    ):
        return "fallback"
    if not maps_enabled:
        return "stencil"
    if target in (
        "projected",
        "point",
        "csm",
        "dynamic",
        "perforated",
        "hybrid",
    ):
        return target
    if not cvar_value_enabled(effective.get("r_shadowmappointlights", "1")):
        return "mixed"
    if cvar_value_enabled(effective.get("r_shadowmapcsm", "0")):
        return "csm"
    return "mapped"


def fog_blend_expectation(args: argparse.Namespace, case_id: str) -> str:
    if case_id not in FOG_BLEND_SCENES:
        return "none"
    effective = {"r_renderersharedworldfogblend": "0"}
    for name, value in args.extra_cvars:
        effective[name.casefold()] = value
    if not cvar_value_enabled(
        effective.get("r_renderersharedworldfogblend", "0")
    ):
        return "disabled"
    if any(
        cvar_value_enabled(effective.get(name, "0"))
        for name in (
            "r_skipfoglights",
            "r_showoverdraw",
            "r_singletriangle",
            "r_skiprender",
            "r_skiprendercontext",
        )
    ):
        return "fallback"
    if cvar_value_enabled(effective.get("r_skipblendlights", "0")):
        return "owned-skip-blend"
    return "owned"


def build_specs(args: argparse.Namespace) -> list[RunSpec]:
    defaults = PROFILE_DEFAULTS[args.profile]
    case_ids = split_csv(args.cases, defaults["cases"])
    tiers = split_csv(args.tiers, defaults["tiers"])
    maxfps_values = split_csv(args.maxfps, defaults["maxfps"])
    swap_values = split_csv(args.swap_intervals, defaults["swap"])
    display_values = split_csv(args.display_modes, defaults["display"])
    shadow_values = split_csv(args.shadow_presets, defaults["shadows"])
    case_shadow_values = defaults.get("caseShadows", {})
    if not args.pacing_only and any(display != "windowed" for display in display_values):
        raise ValueError(
            "per-map CPU/GPU budget evidence requires windowed display; "
            "fullscreen profiles are pacing-only"
        )

    specs: list[RunSpec] = []
    for case_id in case_ids:
        if case_id not in ALL_SCENES:
            raise ValueError(f"unknown case '{case_id}'. Use --list to inspect valid cases.")
        scene = ALL_SCENES[case_id]
        for tier in tiers:
            if tier not in SAFE_TIERS:
                raise ValueError(f"unknown r_glTier '{tier}'")
            for maxfps in maxfps_values:
                for swap in swap_values:
                    for display in display_values:
                        if display not in DISPLAY_MODES:
                            raise ValueError(f"unknown display mode '{display}'")
                        selected_shadow_values = (
                            case_shadow_values.get(case_id, shadow_values)
                            if not args.shadow_presets
                            else shadow_values
                        )
                        for shadow in selected_shadow_values:
                            if shadow not in SHADOW_PRESETS:
                                raise ValueError(f"unknown shadow preset '{shadow}'")
                            specs.append(
                                RunSpec(
                                    case_id=case_id,
                                    mode=scene["mode"],
                                    map_name=scene["map"],
                                    budget_map_name=scene.get("budgetMap", scene["map"]),
                                    purpose=scene["purpose"],
                                    path_name=scene["path"],
                                    tier=tier,
                                    maxfps=maxfps,
                                    swap_interval=swap,
                                    display_mode=display,
                                    shadow_preset=shadow,
                                    renderer=args.renderer,
                                    render_api=args.render_api,
                                    interaction_expectation=interaction_expectation(
                                        args, case_id, shadow
                                    ),
                                    interaction_shadow_expectation=interaction_shadow_expectation(
                                        args, case_id, shadow
                                    ),
                                    fog_blend_expectation=fog_blend_expectation(
                                        args, case_id
                                    ),
                                )
                            )
    if args.limit > 0:
        specs = specs[: args.limit]
    return specs


def write_reports(output_dir: Path, results: list[dict[str, Any]], metadata: dict[str, Any]) -> tuple[Path, Path]:
    report_json = output_dir / "renderer_gameplay_benchmark_report.json"
    report_md = output_dir / "renderer_gameplay_benchmark_report.md"
    report_metadata = {
        key: value
        for key, value in metadata.items()
        if key not in {
            "git",
            "runtime",
            "runtimeVerificationFailures",
            "budgetContract",
            "budgetEnforced",
        }
    }
    payload = {
        "schemaVersion": REPORT_SCHEMA_VERSION,
        "status": (
            "planned"
            if metadata["dryRun"]
            else (
                "pass"
                if results
                and all(item["status"] == "pass" for item in results)
                and not metadata.get("runtimeVerificationFailures", [])
                else "fail"
            )
        ),
        "dryRun": metadata["dryRun"],
        "git": metadata["git"],
        "runtime": metadata["runtime"],
        "runtimeVerificationFailures": metadata.get("runtimeVerificationFailures", []),
        "budgetContract": metadata["budgetContract"],
        "budgetEnforced": metadata["budgetEnforced"],
        "metadata": report_metadata,
        "requiredScenes": REQUIRED_SCENES,
        "worldAmbientScenes": WORLD_AMBIENT_SCENES,
        "interactionScenes": INTERACTION_SCENES,
        "fogBlendScenes": FOG_BLEND_SCENES,
        "loadRegressionScenes": LOAD_REGRESSION_SCENES,
        "shadowScenes": SHADOW_SCENES,
        "campaignTransitionScenes": CAMPAIGN_TRANSITION_SCENES,
        "shadowPresets": SHADOW_PRESETS,
        "results": results,
    }
    report_json.write_text(json.dumps(payload, indent=2), encoding="utf-8")

    passed = sum(1 for result in results if result["status"] == "pass")
    failed = sum(1 for result in results if result["status"] == "fail")
    planned = sum(1 for result in results if result["status"] == "planned")
    lines = [
        "# Renderer Gameplay Benchmark Report",
        "",
        f"- Generated: {metadata['generated']}",
        f"- Host: {metadata['host']}",
        f"- Executable: `{metadata['executable']}`",
        f"- Runtime: `{metadata['runtime']['path']}` ({len(metadata['runtime']['files'])} hashed files)",
        f"- Runtime remained immutable: `{str(not metadata.get('runtimeVerificationFailures', [])).lower()}`",
        f"- Base path: `{metadata['basepath'] or 'not set'}`",
        f"- Profile: `{metadata['profile']}`",
        f"- Benchmark preset: `{metadata['benchmarkPreset']}`",
        f"- Per-map budget contract: `{metadata['budgetContract']['contractId']}` (`{metadata['budgetContract']['sha256']}`)",
        f"- Per-map budgets enforced: `{str(metadata['budgetEnforced']).lower()}`",
        f"- Budget display contract: `{metadata.get('budgetDisplayContract', {}).get('contractId', 'not enforced') if metadata.get('budgetDisplayContract') else 'not enforced'}`",
        f"- Sample: `{metadata['sampleMsec']} ms`" if metadata.get("sampleMsec", 0) > 0 else f"- Sample: `{metadata['sampleFrames']} frames`",
        f"- Cases: {passed} passed, {failed} failed, {planned} planned",
        "",
        "## Results",
        "",
        "| Status | Case | Mode | Map | Tier | FPS | VSync | Display | Shadow | Pacing | Benchmark | Image | Screenshot | Log |",
        "|---|---|---|---|---|---:|---:|---|---|---|---|---|---|---|",
    ]
    for result in results:
        if result["status"] == "planned":
            lines.append(
                f"| planned | `{result['id']}` | {result['mode']} | `{result['map']}` |  |  |  |  |  |  | dry run |  |  |  |"
            )
            continue
        role = next((item for item in result.get("roles", []) if item["role"] in ("client", "sp")), result.get("roles", [{}])[0])
        summary = role.get("summary", {})
        benchmark = summary.get("benchmarkCapture", "")
        if len(benchmark) > 80:
            benchmark = benchmark[:77] + "..."
        pacing = ""
        if summary.get("pacingHz"):
            pacing = f"{summary['pacingHz']} Hz"
            if summary.get("pacingP95Ms"):
                pacing += f" / p95 {summary['pacingP95Ms']} ms"
        image = role.get("image", {}) or {}
        image_status = image.get("status", "missing")
        if image_status == "compared":
            image_status = f"compared rms={image.get('rms', '?')} max={image.get('maxDelta', '?')} pass={int(bool(image.get('pass', False)))}"
        elif image_status in ("not-requested", "reference-not-found"):
            image_status = f"{image_status} {image.get('sha256', '')[:12]}".strip()
        difference = role.get("imageDifference", {}) or {}
        if difference.get("status") != "not-requested":
            image_status += (
                f"; shadow-delta rms={difference.get('rms', '?')} "
                f"channels={difference.get('differingChannels', '?')} "
                f"pass={int(bool(difference.get('pass', False)))}"
            )
        screenshot = role.get("screenshot", "")
        log = role.get("log", "")
        lines.append(
            f"| {result['status']} | `{result['id']}` | {result['mode']} | `{result['map']}` | `{result['tier']}` | {result['maxfps']} | {result['swapInterval']} | {result['display']} | `{result['shadowPreset']}` | {pacing or 'missing'} | {benchmark or 'missing'} | {image_status} | `{screenshot}` | `{log}` |"
        )
        for role_result in result.get("roles", []):
            if role_result.get("missing"):
                lines.append(
                    f"|  | `{role_result['role']}` missing |  |  |  |  |  |  |  | {'; '.join(role_result['missing'])} |  |  |  |  |"
                )

    scene_roles = [
        (result, role) for result in results for role in result.get("roles", [])
        if role.get("sceneEvidence", {}).get("required")
    ]
    if scene_roles:
        lines += [
            "", "## Stock Entry and Camera Evidence", "",
            "Endpoint checks do not prove continuous camera stability or visual parity.", "",
            "| Case / role | Filter | View policy | Status | Position delta | Angle delta |",
            "|---|---|---|---|---|---|",
        ]
        for result, role in scene_roles:
            evidence = role["sceneEvidence"]
            contract = evidence["contract"]
            observed = next((item for item in evidence["observations"] if item["source"] == "log"), {})
            lines.append(
                f"| `{result['id']}` / `{role['role']}` | {contract['entityFilter']} | "
                f"{contract['viewPolicy']} | {evidence['status']} | "
                f"{observed.get('positionDelta', 'missing')} | {observed.get('angleDelta', 'missing')} |"
            )

    budget_roles = [
        (result, role)
        for result in results
        for role in result.get("roles", [])
        if role.get("budgetEvidence")
    ]
    if budget_roles:
        lines += [
            "",
            "## Per-Map CPU/GPU Budget Evidence",
            "",
            "| Status | Case / role | Map | Backend | Profile | CPU samples / P95 / P99 | GPU samples / P95 / P99 | Budget |",
            "|---|---|---|---|---|---|---|---|",
        ]
        for result, role in budget_roles:
            evidence = role["budgetEvidence"]
            measurement = evidence.get("measurement", {})
            cpu = measurement.get("cpu", {})
            gpu = measurement.get("gpu", {})
            gpu_summary = (
                f"{gpu.get('samples', '?')} / {gpu.get('p95Us', '?')} / {gpu.get('p99Us', '?')} us"
                if gpu.get("available")
                else "unavailable"
            )
            lines.append(
                f"| {evidence.get('status', 'fail')} | `{result['id']}` / `{role['role']}` | "
                f"`{measurement.get('map', 'missing')}` | `{measurement.get('backend', 'missing')}` | "
                f"`{measurement.get('profile', 'missing')}` | {cpu.get('samples', '?')} / "
                f"{cpu.get('p95Us', '?')} / {cpu.get('p99Us', '?')} us | {gpu_summary} | "
                f"`{evidence.get('budgetId', 'missing')}` |"
            )

    diagnostic_roles = [
        (result, role_result)
        for result in results
        for role_result in result.get("roles", [])
        if role_result.get("failureDiagnostics")
    ]
    if diagnostic_roles:
        lines += [
            "",
            "## Matched Failure Diagnostics",
            "",
            "The exact matched lines are retained here even when they fall outside the normal log tail.",
        ]
        for result, role_result in diagnostic_roles:
            lines += [
                "",
                f"### `{result['id']}` / `{role_result['role']}`",
                "",
                "```text",
            ]
            lines.extend(format_failure_diagnostic(item) for item in role_result["failureDiagnostics"])
            omitted = role_result.get("failureDiagnosticsOmitted", 0)
            if omitted:
                lines.append(f"... {omitted} additional matching line(s) omitted")
            lines.append("```")

    lines += [
        "",
        "## Required Scene Coverage",
        "",
        "| Case | Mode | Map | Purpose |",
        "|---|---|---|---|",
    ]
    for case_id, scene in REQUIRED_SCENES.items():
        lines.append(f"| `{case_id}` | {scene['mode']} | `{scene['map']}` | {scene['purpose']} |")

    lines += [
        "",
        "## Shadow Correctness Coverage",
        "",
        "| Case | Mode | Map | Purpose |",
        "|---|---|---|---|",
    ]
    for case_id, scene in SHADOW_SCENES.items():
        lines.append(f"| `{case_id}` | {scene['mode']} | `{scene['map']}` | {scene['purpose']} |")

    lines += [
        "",
        "## World Ambient Ownership Coverage",
        "",
        "| Case | Mode | Map | Purpose |",
        "|---|---|---|---|",
    ]
    for case_id, scene in WORLD_AMBIENT_SCENES.items():
        lines.append(f"| `{case_id}` | {scene['mode']} | `{scene['map']}` | {scene['purpose']} |")

    lines += [
        "",
        "## Interaction Ownership Coverage",
        "",
        "| Case | Mode | Map | Purpose |",
        "|---|---|---|---|",
    ]
    for case_id, scene in INTERACTION_SCENES.items():
        lines.append(f"| `{case_id}` | {scene['mode']} | `{scene['map']}` | {scene['purpose']} |")

    lines += [
        "",
        "## Fog/Blend Ownership Coverage",
        "",
        "| Case | Mode | Map | Purpose |",
        "|---|---|---|---|",
    ]
    for case_id, scene in FOG_BLEND_SCENES.items():
        lines.append(f"| `{case_id}` | {scene['mode']} | `{scene['map']}` | {scene['purpose']} |")

    lines += [
        "",
        "## Stock Interaction-Shadow Ownership Coverage",
        "",
        "| Case | Mode | Map | Target | Purpose |",
        "|---|---|---|---|---|",
    ]
    for case_id, scene in INTERACTION_SHADOW_SCENES.items():
        lines.append(
            f"| `{case_id}` | {scene['mode']} | `{scene['map']}` | "
            f"`{scene['interactionShadowTarget']}` | {scene['purpose']} |"
        )

    lines += [
        "",
        "## Load/Shadow Regression Coverage",
        "",
        "| Case | Mode | Map | Purpose |",
        "|---|---|---|---|",
    ]
    for case_id, scene in LOAD_REGRESSION_SCENES.items():
        lines.append(f"| `{case_id}` | {scene['mode']} | `{scene['map']}` | {scene['purpose']} |")

    lines += [
        "",
        "## Shadow Presets",
        "",
        "| Preset | Cvars |",
        "|---|---|",
    ]
    for preset, cvars in SHADOW_PRESETS.items():
        cvar_text = ", ".join(f"`{key} {value}`" for key, value in cvars.items()) or "stock defaults"
        lines.append(f"| `{preset}` | {cvar_text} |")

    report_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return report_json, report_md


def _verified_artifact(
    report_dir: Path, record: Any, kind: str
) -> tuple[Path | None, list[str]]:
    if not isinstance(record, dict) or record.get("kind") != kind:
        return None, [f"recorded {kind} artifact is missing or malformed"]
    raw_path = record.get("path")
    if not isinstance(raw_path, str) or not raw_path or Path(raw_path).is_absolute():
        return None, [f"recorded {kind} artifact path is not a safe relative path"]
    path = (report_dir / Path(raw_path)).resolve()
    try:
        path.relative_to(report_dir.resolve())
    except ValueError:
        return None, [f"recorded {kind} artifact escapes the report directory"]
    if not path.is_file():
        return None, [f"recorded {kind} artifact is missing: {raw_path}"]
    actual = {"kind": kind, **file_record(path, report_dir)}
    return path, ([] if actual == record else [f"recorded {kind} artifact differs: {raw_path}"])


def verify_benchmark_report(
    report: Any,
    report_dir: Path,
    root: Path,
    runtime_dir: Path,
    executable: Path,
    contract: dict[str, Any],
    contract_binding: dict[str, Any],
) -> list[str]:
    if not isinstance(report, dict):
        return ["benchmark report root is not an object"]
    failures: list[str] = []
    if report.get("schemaVersion") != REPORT_SCHEMA_VERSION:
        failures.append(f"unsupported benchmark report schema: {report.get('schemaVersion')!r}")
    if report.get("status") != "pass":
        failures.append(f"benchmark report status is {report.get('status')!r}, not 'pass'")
    if report.get("dryRun") is not False:
        failures.append("passing benchmark evidence must record dryRun=false")
    recorded_budget_enforced = report.get("budgetEnforced")
    if type(recorded_budget_enforced) is not bool:
        failures.append("benchmark report budget-enforcement mode is missing or malformed")
        budget_enforced = True
    else:
        budget_enforced = recorded_budget_enforced
    if report.get("runtimeVerificationFailures") != []:
        failures.append("benchmark report recorded runtime mutation during capture")
    failures.extend(
        verify_contract_binding(report.get("budgetContract"), contract, contract_binding)
    )

    current_git = git_state(root)
    if report.get("git") != current_git:
        failures.append("benchmark Git provenance differs from the current checkout")
    current_files = collect_runtime_files(runtime_dir)
    runtime = report.get("runtime")
    if not isinstance(runtime, dict):
        failures.append("benchmark runtime binding is missing or malformed")
    else:
        if runtime.get("path") != path_hint(runtime_dir, root):
            failures.append("benchmark runtime path binding differs")
        try:
            executable_path = executable.relative_to(runtime_dir).as_posix()
        except ValueError:
            executable_path = ""
        if runtime.get("executable") != executable_path:
            failures.append("benchmark executable binding differs")
        failures.extend(compare_file_records(runtime.get("files"), current_files, "runtime file"))

    metadata = report.get("metadata")
    expected_display_contract = budget_display_contract()
    if not isinstance(metadata, dict):
        failures.append("benchmark metadata is missing or malformed")
        metadata = {}
    expected_metadata_display_contract = (
        expected_display_contract if budget_enforced else None
    )
    if (
        "budgetDisplayContract" not in metadata
        or metadata.get("budgetDisplayContract") != expected_metadata_display_contract
    ):
        failures.append(
            "benchmark budget display contract differs from the recorded capture mode"
        )
    benchmark_profile = metadata.get("benchmarkPreset")
    if not isinstance(benchmark_profile, str) or not benchmark_profile:
        failures.append("benchmark preset provenance is missing")
        benchmark_profile = ""
    recorded_render_api = metadata.get("renderApi")
    if recorded_render_api not in ("gl", "vk"):
        failures.append("benchmark renderer API provenance is missing or malformed")
        recorded_render_api = ""
    pbr_fixture_acceptance_required = False
    recorded_post_map_cvars = metadata.get("effectivePostMapCvars")
    valid_post_map_cvars = isinstance(recorded_post_map_cvars, dict) and all(
        isinstance(name, str)
        and name == name.casefold()
        and re.fullmatch(r"[a-z_][a-z0-9_]*", name) is not None
        and isinstance(value, str)
        and bool(value)
        and not has_command_delimiter_or_control(value)
        and (
            name not in PBR_PROTECTED_CVARS
            or (
                not has_engine_token_expansion_or_comment(value)
                and not any(character in value for character in ('"', "'", "\\"))
                and not (name in PBR_BOOL_CVARS and value.startswith("+"))
            )
        )
        for name, value in (
            recorded_post_map_cvars.items()
            if isinstance(recorded_post_map_cvars, dict)
            else ()
        )
    )
    if not valid_post_map_cvars:
        failures.append("effective post-map cvar provenance is missing or malformed")
        recorded_post_map_cvars = {}
    elif recorded_render_api:
        pbr_fixture_acceptance_required = requires_pbr_fixture_acceptance(
            recorded_post_map_cvars.items(), recorded_render_api
        )
    recorded_pbr_required = metadata.get("pbrFixtureAcceptanceRequired")
    if type(recorded_pbr_required) is not bool:
        failures.append("PBR fixture acceptance provenance is missing or malformed")
    elif recorded_pbr_required is not pbr_fixture_acceptance_required:
        failures.append("PBR fixture acceptance provenance differs")

    if "pbrFixtureBinding" not in metadata:
        failures.append("PBR fixture binding provenance is missing")
    recorded_fixture_binding = metadata.get("pbrFixtureBinding")
    if pbr_fixture_acceptance_required:
        try:
            current_fixture_binding = verify_procedural_pbr_fixture(
                runtime_dir, PBR_FIXTURE_TEXTURE_SIZE
            )
        except (FileNotFoundError, OSError, RuntimeError, ValueError) as exc:
            failures.append(f"controlled PBR fixture verification failed: {exc}")
        else:
            if recorded_fixture_binding != current_fixture_binding:
                failures.append("controlled PBR fixture binding differs")
    elif recorded_fixture_binding is not None:
        failures.append("ordinary benchmark report must not bind a controlled PBR fixture")
    if not budget_enforced and not pbr_fixture_acceptance_required:
        failures.append(
            "functional replay is accepted only for the controlled PBR fixture"
        )
    recorded_exec_values = metadata.get("execCommands")
    if not isinstance(recorded_exec_values, list) or not all(
        isinstance(command, str) for command in recorded_exec_values
    ):
        failures.append("benchmark exec-command provenance is missing or malformed")
        recorded_exec_commands: tuple[str, ...] = ()
    else:
        try:
            recorded_exec_commands = parse_exec_commands(recorded_exec_values)
        except ValueError as exc:
            failures.append(f"benchmark exec-command provenance is invalid: {exc}")
            recorded_exec_commands = ()
    results = report.get("results")
    if not isinstance(results, list) or not results:
        return [*failures, "benchmark results are missing or empty"]
    if not budget_enforced and len(results) != 1:
        failures.append("functional replay requires exactly one controlled result")
    for result in results:
        if not isinstance(result, dict):
            failures.append("benchmark result is malformed")
            continue
        case_id = str(result.get("id", "unknown"))
        scene_id = result.get("caseId")
        scene = ALL_SCENES.get(scene_id) if isinstance(scene_id, str) else None
        scene_contract = scene_view_contract(scene_id) if scene else None
        identity = [result.get(field) for field in (
            "tier", "maxfps", "swapInterval", "display", "shadowPreset", "renderer"
        )]
        if scene is None or not all(isinstance(value, str) and value for value in identity):
            failures.append(f"{case_id}: scene case identity is missing or malformed")
        else:
            expected_id = sanitize_case_id("_".join((
                scene_id, identity[0], f"fps{identity[1]}", f"vsync{identity[2]}", *identity[3:]
            )))
            if (case_id != expected_id or result.get("mode") != scene["mode"]
                    or result.get("map") != scene["map"]
                    or result.get("budgetMap") != scene.get("budgetMap", scene["map"])):
                failures.append(f"{case_id}: scene case/map identity differs from the catalog")
        if "sceneContract" not in result or result["sceneContract"] != scene_contract:
            failures.append(f"{case_id}: scene contract differs from the catalog")
        if result.get("status") != "pass":
            failures.append(f"{case_id}: result is not a pass")
        if not budget_enforced:
            identity_fields = (
                result.get("tier"),
                result.get("maxfps"),
                result.get("swapInterval"),
                result.get("display"),
                result.get("shadowPreset"),
                result.get("renderer"),
            )
            expected_fixture_id = (
                sanitize_case_id(
                    "_".join(
                        (
                            PBR_FIXTURE_BENCHMARK_CASE,
                            identity_fields[0],
                            f"fps{identity_fields[1]}",
                            f"vsync{identity_fields[2]}",
                            identity_fields[3],
                            identity_fields[4],
                            identity_fields[5],
                        )
                    )
                )
                if all(isinstance(value, str) and value for value in identity_fields)
                else ""
            )
            if (
                result.get("mode") != "SP"
                or normalize_engine_map_name(str(result.get("map", "")))
                != normalize_engine_map_name(PBR_FIXTURE_STOCK_MAP)
                or normalize_engine_map_name(str(result.get("budgetMap", "")))
                != normalize_engine_map_name(PBR_FIXTURE_STOCK_MAP)
                or case_id != expected_fixture_id
            ):
                failures.append(
                    f"{case_id}: functional replay case/result identity differs from the controlled fixture"
                )
        expected_map = result.get("budgetMap")
        if not isinstance(expected_map, str) or not expected_map:
            failures.append(f"{case_id}: budgetMap identity is missing")
            continue
        expected_backend = result.get("expectedBackend")
        render_api = result.get("renderApi")
        if render_api != recorded_render_api:
            failures.append(f"{case_id}: renderer API differs from report provenance")
        derived_backend = "vulkan" if render_api == "vk" else (
            "opengl" if render_api == "gl" else ""
        )
        if expected_backend not in ("opengl", "vulkan") or expected_backend != derived_backend:
            failures.append(f"{case_id}: expected backend/launch render API binding differs")
            continue
        if result.get("display") != "windowed":
            failures.append(f"{case_id}: budget evidence was not captured windowed")
        if result.get("displayContract") != expected_display_contract:
            failures.append(f"{case_id}: launch display contract differs from bordered 1280x720")
        if (
            not budget_enforced
            and render_api == "gl"
            and result.get("tier") not in GL_TIER_RUNTIME_NAMES
        ):
            failures.append(
                f"{case_id}: controlled GL replay must request an explicit supported tier"
            )
        if type(result.get("pbrFixtureAcceptanceRequired")) is not bool:
            failures.append(f"{case_id}: PBR fixture acceptance selection is missing")
        elif (
            result.get("pbrFixtureAcceptanceRequired")
            is not pbr_fixture_acceptance_required
        ):
            failures.append(f"{case_id}: PBR fixture acceptance selection differs")
        roles = result.get("roles")
        if not isinstance(roles, list) or not roles:
            failures.append(f"{case_id}: role evidence is missing")
            continue
        if not budget_enforced and (
            len(roles) != 1
            or not isinstance(roles[0], dict)
            or roles[0].get("role") != "sp"
        ):
            failures.append(f"{case_id}: functional replay requires exactly one SP role")
        for role in roles:
            if not isinstance(role, dict):
                failures.append(f"{case_id}: role evidence is malformed")
                continue
            role_name = str(role.get("role", "unknown"))
            if role.get("status") != "pass" or role.get("missing") != []:
                failures.append(f"{case_id}/{role_name}: role is not a clean pass")
            if type(role.get("exitCode")) is not int or role.get("exitCode") != 0:
                failures.append(f"{case_id}/{role_name}: process exit code is not zero")
            if role.get("timedOut") is not False:
                failures.append(f"{case_id}/{role_name}: process timed-out state is not false")
            artifact_values = role.get("artifacts")
            artifacts = {
                item.get("kind"): item
                for item in artifact_values
                if isinstance(item, dict) and item.get("kind")
            } if isinstance(artifact_values, list) else {}
            source_pairs: list[tuple[str, str]] = []
            source_names = {
                "engineLog": "log",
                "processStdout": "stdout",
                "processStderr": "stderr",
            }
            for kind in ("engineLog", "processStdout", "processStderr"):
                path, artifact_failures = _verified_artifact(
                    report_dir, artifacts.get(kind), kind
                )
                failures.extend(f"{case_id}/{role_name}: {item}" for item in artifact_failures)
                if path is not None:
                    source_pairs.append(
                        (
                            source_names[kind],
                            path.read_text(encoding="utf-8", errors="replace"),
                        )
                    )
            sources = [source_text for _, source_text in source_pairs]
            config_path, config_failures = _verified_artifact(
                report_dir, artifacts.get("benchmarkConfig"), "benchmarkConfig"
            )
            failures.extend(
                f"{case_id}/{role_name}: {item}" for item in config_failures
            )
            if config_path is not None:
                try:
                    config_evidence = parse_benchmark_config(
                        config_path.read_text(encoding="utf-8")
                    )
                except (OSError, UnicodeError, ValueError) as exc:
                    failures.append(
                        f"{case_id}/{role_name}: benchmark config provenance is invalid: {exc}"
                    )
                else:
                    expected_scene = (
                        (scene_contract["map"], scene_contract["entityFilter"])
                        if scene_contract else ("", "")
                    )
                    if (config_evidence.scene_map, config_evidence.entity_filter) != expected_scene:
                        failures.append(f"{case_id}/{role_name}: benchmark config scene binding differs")
                    config_cvars = config_evidence.cvars
                    config_commands = config_evidence.commands
                    config_effective_cvars = effective_post_map_cvars(config_cvars)
                    if config_effective_cvars != recorded_post_map_cvars:
                        failures.append(
                            f"{case_id}/{role_name}: benchmark config CVar provenance differs"
                        )
                    if render_api in ("gl", "vk") and (
                        requires_pbr_fixture_acceptance(config_cvars, render_api)
                        is not pbr_fixture_acceptance_required
                    ):
                        failures.append(
                            f"{case_id}/{role_name}: benchmark config PBR selection differs"
                        )
                    expected_config_commands = recorded_exec_commands + (
                        (f"waitMsec {MP_SERVER_CLIENT_GRACE_MSEC}",)
                        if role_name == "server"
                        else ()
                    )
                    if config_commands != expected_config_commands:
                        failures.append(
                            f"{case_id}/{role_name}: benchmark config command provenance differs"
                        )
                    expected_capture_mode = (
                        "budget" if budget_enforced else "functional-replay"
                    )
                    if config_evidence.capture_mode != expected_capture_mode:
                        failures.append(
                            f"{case_id}/{role_name}: benchmark config capture mode differs"
                        )
                    if (
                        config_evidence.role != role_name
                        or config_evidence.screenshot_request
                        != role.get("screenshotRequest")
                    ):
                        failures.append(
                            f"{case_id}/{role_name}: benchmark config role/screenshot binding differs"
                        )
                    recorded_settle_frames = metadata.get("settleFrames")
                    recorded_mp_delay_frames = metadata.get("mpClientDelayFrames")
                    if (
                        type(recorded_settle_frames) is not int
                        or (
                            role_name == "server"
                            and type(recorded_mp_delay_frames) is not int
                        )
                    ):
                        failures.append(
                            f"{case_id}/{role_name}: benchmark settle provenance is malformed"
                        )
                    else:
                        expected_settle_frames = max(1, recorded_settle_frames)
                        if role_name == "server":
                            expected_settle_frames = max(
                                1, recorded_settle_frames + recorded_mp_delay_frames
                            )
                        if scene_contract:
                            expected_settle_frames = max(
                                expected_settle_frames, scene_contract["minimumSettleFrames"]
                            )
                        expected_settle_msec = (
                            scene_contract.get("minimumSettleMsec", 0) if scene_contract else 0
                        )
                        if (
                            config_evidence.settle_frames != expected_settle_frames
                            or config_evidence.settle_msec != expected_settle_msec
                        ):
                            failures.append(
                                f"{case_id}/{role_name}: benchmark config settle interval differs"
                            )
                    recorded_sample_msec = metadata.get("sampleMsec")
                    recorded_sample_frames = metadata.get("sampleFrames")
                    if (
                        type(recorded_sample_msec) is not int
                        or type(recorded_sample_frames) is not int
                    ):
                        failures.append(
                            f"{case_id}/{role_name}: benchmark sample provenance is malformed"
                        )
                    else:
                        expected_sample_kind = (
                            "waitMsec" if recorded_sample_msec > 0 else "wait"
                        )
                        expected_sample_value = max(
                            1,
                            recorded_sample_msec
                            if recorded_sample_msec > 0
                            else recorded_sample_frames,
                        )
                        if (
                            config_evidence.sample_kind != expected_sample_kind
                            or config_evidence.sample_value != expected_sample_value
                        ):
                            failures.append(
                                f"{case_id}/{role_name}: benchmark config sample interval differs"
                            )
            screenshot_path, screenshot_failures = _verified_artifact(
                report_dir, artifacts.get("screenshot"), "screenshot"
            )
            failures.extend(
                f"{case_id}/{role_name}: {item}" for item in screenshot_failures
            )
            display_evidence, display_failures = evaluate_display_evidence(
                sources, screenshot_path
            )
            failures.extend(
                f"{case_id}/{role_name}: display evidence: {item}"
                for item in display_failures
            )
            if role.get("displayEvidence") != display_evidence:
                failures.append(
                    f"{case_id}/{role_name}: recorded display evidence differs"
                )
            combined_sources = "\n".join(sources)
            recorded_warnings = role.get("warnings")
            current_warnings = warning_counts(combined_sources)
            if recorded_warnings != current_warnings:
                failures.append(f"{case_id}/{role_name}: recorded warning evidence differs")
            if any(count > 0 for count in current_warnings.values()):
                failures.append(f"{case_id}/{role_name}: warning evidence is not clean")
            pbr_summary = extract_summary(combined_sources)
            pbr_evidence, pbr_failures = evaluate_pbr_fixture_evidence(
                pbr_summary, pbr_fixture_acceptance_required, render_api
            )
            failures.extend(
                f"{case_id}/{role_name}: {item}" for item in pbr_failures
            )
            if role.get("pbrFixtureEvidence") != pbr_evidence:
                failures.append(
                    f"{case_id}/{role_name}: recorded PBR fixture evidence differs"
                )
            map_evidence, map_failures = evaluate_controlled_map_evidence(
                source_pairs, pbr_fixture_acceptance_required
            )
            failures.extend(
                f"{case_id}/{role_name}: {item}" for item in map_failures
            )
            if role.get("mapEvidence") != map_evidence:
                failures.append(
                    f"{case_id}/{role_name}: recorded controlled-map evidence differs"
                )
            scene_evidence, scene_failures = evaluate_scene_view_evidence(source_pairs, scene_contract)
            failures.extend(f"{case_id}/{role_name}: {item}" for item in scene_failures)
            if role.get("sceneEvidence") != scene_evidence:
                failures.append(f"{case_id}/{role_name}: recorded scene evidence differs")
            if not budget_enforced and render_api == "gl":
                selected_tiers = [
                    match.group(1).casefold()
                    for source_name, source_text in source_pairs
                    for match in re.finditer(
                        r"^Selected renderer tier:\s*([A-Za-z0-9]+)\s*$",
                        source_text,
                        re.IGNORECASE | re.MULTILINE,
                    )
                    if source_name == "log"
                ]
                expected_runtime_tier = GL_TIER_RUNTIME_NAMES.get(result.get("tier"))
                if (
                    expected_runtime_tier is None
                    or not selected_tiers
                    or selected_tiers[-1].casefold()
                    != expected_runtime_tier.casefold()
                ):
                    failures.append(
                        f"{case_id}/{role_name}: final GL selected tier differs from the result"
                    )
            if budget_enforced:
                failures.extend(
                    f"{case_id}/{role_name}: {item}"
                    for item in verify_recorded_evidence(
                        role.get("budgetEvidence"),
                        sources,
                        contract,
                        expected_map,
                        expected_backend,
                        benchmark_profile,
                    )
                )
            elif role.get("budgetEvidence") != {}:
                failures.append(
                    f"{case_id}/{role_name}: functional replay budget evidence must be empty"
                )
    return failures


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=tuple(PROFILE_DEFAULTS.keys()), default="smoke", help="Preset case/dimension profile.")
    parser.add_argument("--cases", default="", help="Comma-separated case ids. Overrides profile cases.")
    parser.add_argument("--tiers", default="", help="Comma-separated r_glTier values. Overrides profile tiers.")
    parser.add_argument("--maxfps", default="", help="Comma-separated com_maxfps values. Overrides profile values.")
    parser.add_argument("--swap-intervals", default="", help="Comma-separated r_swapInterval values. Overrides profile values.")
    parser.add_argument("--display-modes", default="", help="Comma-separated display modes: windowed,fullscreen.")
    parser.add_argument("--width", type=int, default=BUDGET_WIDTH, help=f"Drawable width. Budget evidence requires {BUDGET_WIDTH}.")
    parser.add_argument("--height", type=int, default=BUDGET_HEIGHT, help=f"Drawable height. Budget evidence requires {BUDGET_HEIGHT}.")
    parser.add_argument("--shadow-presets", default="", help="Comma-separated shadow presets. Use --list to inspect values.")
    parser.add_argument("--renderer", default="best", help="Value for r_renderer, usually best or arb2.")
    parser.add_argument("--render-api", choices=("gl", "vk"), default="gl", help="Exact renderer backend launch contract: gl or vk.")
    parser.add_argument("--benchmark-preset", default="baseline", help="Value for r_rendererBenchmarkPreset.")
    parser.add_argument("--modern-executor", action="store_true", help="Opt into r_rendererModernExecutor for gameplay benchmarking. Defaults off so ARB2/high-FPS baselines are not polluted by side-path work.")
    parser.add_argument("--gpu-timers", action=argparse.BooleanOptionalAction, default=True, help="Enable backend-neutral whole-frame GPU timestamps during sampled budget runs. Enabled by default; --no-gpu-timers is only valid with --pacing-only.")
    parser.add_argument("--show-fps-overlay", action="store_true", help="Draw the in-game FPS overlay during the run. Defaults off so acceptance timings measure renderer/gameplay cost, not diagnostic text drawing.")
    parser.add_argument("--pacing-only", action="store_true", help="Measure frame pacing without enabling r_rendererMetrics or rendererBenchmarkCapture. Use this for high-FPS acceptance runs after diagnostic captures are clean.")
    parser.add_argument("--min-pacing-hz", type=float, default=0.0, help="Fail when the parsed frame-pacing snapshot falls below this average presentation rate.")
    parser.add_argument("--max-p95-ms", type=float, default=0.0, help="Fail when the parsed frame-pacing P95 exceeds this millisecond budget. Use 0 to disable.")
    parser.add_argument("--max-p99-ms", type=float, default=0.0, help="Fail when the parsed frame-pacing P99 exceeds this millisecond budget. Use 0 to disable.")
    parser.add_argument("--max-frame-ms", type=float, default=0.0, help="Fail when any sampled frame exceeds this millisecond budget. Use 0 to disable; the engine reports maxima to 0.01 ms.")
    parser.add_argument("--set-cvar", action="append", default=[], metavar="NAME=VALUE", help="Extra post-map cvar written into the generated benchmark cfg. Repeat for A/B diagnostics without extending the launch command line.")
    parser.add_argument("--set-launch-cvar", action="append", default=[], metavar="NAME=VALUE", help="Extra cvar applied on the openQ4 launch command line before the map loads. Use for load-time renderer knobs such as vertex/index buffer caching.")
    parser.add_argument("--exec-command", action="append", default=[], metavar="COMMAND", help="Extra post-map console command written into the generated benchmark cfg. Repeat for targeted diagnostics such as flashlight impulses.")
    parser.add_argument("--autoexec-delay-ms", type=int, default=1000, help="Delay after active map draw before executing the generated benchmark cfg.")
    parser.add_argument("--settle-frames", type=int, default=360, help="Frames to wait after map/connect before sampling.")
    parser.add_argument("--sample-frames", type=int, default=600, help="Frames to sample before dumping metrics and screenshots.")
    parser.add_argument("--sample-msec", type=int, default=0, help="Real milliseconds to sample before dumping metrics and screenshots. Overrides --sample-frames when positive.")
    parser.add_argument("--timeout", type=int, default=180, help="Per-case process timeout in seconds.")
    parser.add_argument("--basepath", default=default_basepath(), help="Quake 4 install/base path. Omit or set empty to skip fs_basepath.")
    parser.add_argument("--runtime-dir", default="", help="Staged runtime package. Defaults to .install; alternates must be named ordinary directories below .tmp/stock-runtime/.")
    parser.add_argument("--budget-contract", default=str(DEFAULT_CONTRACT_PATH), help="Versioned per-map CPU/GPU budget JSON. Its stable id and SHA-256 are bound into the report.")
    parser.add_argument("--verify-report", default="", help="Replay-verify a prior real benchmark report, runtime, artifacts, timing marker, and budget contract without launching openQ4.")
    parser.add_argument("--output-dir", default="", help="Report/output directory. Defaults to <repo>/.tmp/renderer-gameplay/<timestamp>.")
    parser.add_argument("--reference-dir", default="", help="Optional TGA reference screenshot root for deterministic image comparison.")
    parser.add_argument("--require-references", action="store_true", help="Fail captures when --reference-dir has no matching reference image.")
    parser.add_argument("--image-rms-threshold", type=float, default=2.0, help="Allowed RMS channel delta for TGA comparisons.")
    parser.add_argument("--image-max-threshold", type=int, default=24, help="Allowed maximum channel delta for TGA comparisons.")
    parser.add_argument("--difference-reference-dir", default="", help="Optional engine-TGA reference root that each eligible interaction-shadow, fog/blend, or material-deform capture must differ from; use a feature-disabled capture to prove the controlled effect is visible.")
    parser.add_argument("--image-difference-min-rms", type=float, default=0.1, help="Minimum RMS channel delta required by --difference-reference-dir.")
    parser.add_argument("--image-difference-min-channels", type=int, default=1000, help="Minimum changed RGB-channel count required by --difference-reference-dir.")
    parser.add_argument("--mp-port", type=int, default=28110, help="Base listen-server port for MP runs.")
    parser.add_argument("--mp-client-delay", type=int, default=12, help="Seconds to wait before launching the MP loopback client.")
    parser.add_argument("--mp-client-delay-frames", type=int, default=480, help="Extra server frames before server-side capture in MP runs.")
    parser.add_argument("--limit", type=int, default=0, help="Limit generated specs, useful for bounded local smoke runs.")
    parser.add_argument("--dry-run", action="store_true", help="Write the planned command lines without launching openQ4.")
    parser.add_argument("--list", action="store_true", help="List profiles, cases, and shadow presets without running.")
    parsed = parser.parse_args(argv)
    if not math.isfinite(parsed.max_frame_ms) or parsed.max_frame_ms < 0:
        parser.error("--max-frame-ms must be finite and non-negative")
    if not parsed.pacing_only and not parsed.gpu_timers:
        parser.error("--no-gpu-timers is only valid with --pacing-only; budget evidence requires GPU timing")
    if not (320 <= parsed.width <= 16384) or not (240 <= parsed.height <= 16384):
        parser.error("--width/--height must stay within the engine's 320x240 to 16384x16384 range")
    if parsed.image_difference_min_rms < 0.0:
        parser.error("--image-difference-min-rms must be non-negative")
    if parsed.image_difference_min_channels < 1:
        parser.error("--image-difference-min-channels must be positive")
    if not parsed.pacing_only and (parsed.width, parsed.height) != (BUDGET_WIDTH, BUDGET_HEIGHT):
        parser.error(
            f"budget evidence requires the canonical bordered {BUDGET_WIDTH}x{BUDGET_HEIGHT} display contract"
        )
    selected_displays = split_csv(
        parsed.display_modes, tuple(PROFILE_DEFAULTS[parsed.profile]["display"])
    )
    if not parsed.pacing_only and any(display != "windowed" for display in selected_displays):
        parser.error(
            "budget evidence requires windowed display; use --pacing-only for fullscreen presentation tests"
        )
    try:
        profile_cvars = tuple(PROFILE_DEFAULTS[parsed.profile].get("cvars", ()))
        profile_launch_cvars = tuple(PROFILE_DEFAULTS[parsed.profile].get("launchCvars", ()))
        profile_exec_commands = tuple(PROFILE_DEFAULTS[parsed.profile].get("execCommands", ()))
        parsed.extra_cvars = profile_cvars + parse_extra_cvars(parsed.set_cvar)
        parsed.launch_cvars = profile_launch_cvars + parse_extra_cvars(parsed.set_launch_cvar)
        parsed.exec_commands = parse_exec_commands(
            [*profile_exec_commands, *parsed.exec_command]
        )
    except ValueError as exc:
        parser.error(str(exc))
    pbr_launch_conflicts = sorted(
        name
        for name, _ in parsed.launch_cvars
        if name.casefold() in PBR_PROTECTED_CVARS
    )
    if pbr_launch_conflicts:
        parser.error(
            "protected PBR fixture CVars must be set post-map with --set-cvar: "
            + ", ".join(pbr_launch_conflicts)
        )
    if not parsed.pacing_only:
        protected_launch_cvars = {
            name.casefold() for name in budget_display_contract()["cvars"]
        } | {"r_renderapi"}
        conflicting = sorted(
            name for name, _ in parsed.launch_cvars if name.casefold() in protected_launch_cvars
        )
        if conflicting:
            parser.error(
                "budget evidence launch CVars may not override the display/backend contract: "
                + ", ".join(conflicting)
            )
    parsed.reference_dir_path = Path(parsed.reference_dir).resolve() if parsed.reference_dir else None
    if parsed.require_references and parsed.reference_dir_path is None:
        parser.error("--require-references requires --reference-dir")
    parsed.difference_reference_dir_path = (
        Path(parsed.difference_reference_dir).resolve()
        if parsed.difference_reference_dir
        else None
    )
    return parsed


def print_list() -> None:
    print("Profiles:")
    for profile, defaults in PROFILE_DEFAULTS.items():
        case_shadows = defaults.get("caseShadows")
        case_shadow_count = (
            sum(len(case_shadows.get(case_id, defaults["shadows"])) for case_id in defaults["cases"])
            if case_shadows
            else len(defaults["cases"]) * len(defaults["shadows"])
        )
        count = (
            case_shadow_count
            * len(defaults["tiers"])
            * len(defaults["maxfps"])
            * len(defaults["swap"])
            * len(defaults["display"])
        )
        profile_cvars = defaults.get("cvars", ())
        profile_exec_commands = defaults.get("execCommands", ())
        annotations: list[str] = []
        if profile_cvars:
            annotations.append("cvars " + ", ".join(f"{key}={value}" for key, value in profile_cvars))
        if profile_exec_commands:
            annotations.append(f"{len(profile_exec_commands)} scripted command(s)")
        annotation_text = " - " + "; ".join(annotations) if annotations else ""
        print(f"  {profile}: {count} generated case(s){annotation_text}")
    print("\nRequired gameplay cases:")
    for case_id, scene in REQUIRED_SCENES.items():
        print(f"  {case_id}: {scene['mode']} {scene['map']} - {scene['purpose']}")
    print("\nShadow correctness cases:")
    for case_id, scene in SHADOW_SCENES.items():
        print(f"  {case_id}: {scene['mode']} {scene['map']} - {scene['purpose']}")
    print("\nCampaign transition cases:")
    for case_id, scene in CAMPAIGN_TRANSITION_SCENES.items():
        print(f"  {case_id}: {scene['mode']} {scene['map']} - {scene['purpose']}")
    print("\nWorld ambient ownership cases (run with --pacing-only):")
    for case_id, scene in WORLD_AMBIENT_SCENES.items():
        print(f"  {case_id}: {scene['mode']} {scene['map']} - {scene['purpose']}")
    print("\nInteraction ownership cases (run with --pacing-only):")
    for case_id, scene in INTERACTION_SCENES.items():
        print(f"  {case_id}: {scene['mode']} {scene['map']} - {scene['purpose']}")
    print("\nFog/blend ownership cases (run with --pacing-only):")
    for case_id, scene in FOG_BLEND_SCENES.items():
        print(f"  {case_id}: {scene['mode']} {scene['map']} - {scene['purpose']}")
    print("\nMaterial-deform ownership cases (run with --pacing-only):")
    for case_id, scene in DEFORM_SCENES.items():
        print(f"  {case_id}: {scene['mode']} {scene['map']} - {scene['purpose']}")
    print("\nStock interaction-shadow cases (run with --pacing-only):")
    for case_id, scene in INTERACTION_SHADOW_SCENES.items():
        print(
            f"  {case_id}: {scene['mode']} {scene['map']} "
            f"[{scene['interactionShadowTarget']}] - {scene['purpose']}"
        )
    print("\nShadow presets:")
    for preset, cvars in SHADOW_PRESETS.items():
        cvar_text = ", ".join(f"{key}={value}" for key, value in cvars.items()) or "stock defaults"
        print(f"  {preset}: {cvar_text}")

def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.list:
        print_list()
        return 0

    root = repo_root()
    runtime_dir = validate_runtime_dir(
        Path(args.runtime_dir) if args.runtime_dir else root / ".install", root
    )
    args.runtime_dir_path = runtime_dir
    executable = find_client_executable(runtime_dir)
    runtime_files_before = collect_runtime_files(runtime_dir)
    budget_contract_path = Path(args.budget_contract)
    args.budget_contract, budget_binding = load_contract(budget_contract_path)
    if args.verify_report:
        report_path = Path(args.verify_report).resolve()
        report = json.loads(report_path.read_text(encoding="utf-8"))
        failures = verify_benchmark_report(
            report,
            report_path.parent,
            root,
            runtime_dir,
            executable,
            args.budget_contract,
            budget_binding,
        )
        for failure in failures:
            print(f"error: {failure}", file=sys.stderr)
        if not failures:
            print("renderer gameplay benchmark verification: pass")
        return 1 if failures else 0
    args.pbr_fixture_acceptance_required = requires_pbr_fixture_acceptance(
        args.extra_cvars, args.render_api
    )
    args.pbr_fixture_binding = (
        verify_procedural_pbr_fixture(runtime_dir, PBR_FIXTURE_TEXTURE_SIZE)
        if args.pbr_fixture_acceptance_required
        else None
    )
    requested_basepath = args.basepath
    basepath = resolve_basepath(requested_basepath)
    if requested_basepath and not basepath:
        print(f"warning: basepath does not exist, omitting fs_basepath: {requested_basepath}", file=sys.stderr)
    if args.reference_dir_path is not None and not args.reference_dir_path.exists():
        raise FileNotFoundError(f"reference directory does not exist: {args.reference_dir_path}")
    if (
        args.difference_reference_dir_path is not None
        and not args.difference_reference_dir_path.exists()
    ):
        raise FileNotFoundError(
            "difference reference directory does not exist: "
            f"{args.difference_reference_dir_path}"
        )

    specs = build_specs(args)
    timestamp = time.strftime("%Y%m%d-%H%M%S")
    output_dir = Path(args.output_dir).resolve() if args.output_dir else root / ".tmp" / "renderer-gameplay" / timestamp
    prepare_output_directory(output_dir)
    run_id = output_dir.name

    results: list[dict[str, Any]] = []
    for index, spec in enumerate(specs):
        print(f"running {spec.id} ({spec.mode} {spec.map_name})...", flush=True)
        try:
            if spec.mode == "MP":
                result = run_mp_spec(root, executable, output_dir, basepath, run_id, spec, index, args)
            else:
                result = run_sp_spec(root, executable, output_dir, basepath, run_id, spec, args)
        except Exception as exc:
            result = harness_failure_result(spec, exc)
            print(f"  fail ({type(exc).__name__}: {exc})", file=sys.stderr, flush=True)
        else:
            print(f"  {result['status']}", flush=True)
        results.append(result)

    runtime_files_after = collect_runtime_files(runtime_dir)
    runtime_verification_failures = compare_file_records(
        runtime_files_before, runtime_files_after, "runtime file"
    )
    if runtime_verification_failures:
        for result in results:
            result["status"] = "fail"
            for role in result.get("roles", []):
                role["status"] = "fail"
                role.setdefault("missing", []).extend(
                    f"runtime mutation: {failure}"
                    for failure in runtime_verification_failures
                )
    attach_result_artifacts(output_dir, results)

    metadata = {
        "generated": time.strftime("%Y-%m-%d %H:%M:%S %z"),
        "host": f"{platform.system()} {platform.release()} {platform.machine()}",
        "executable": str(executable),
        "runtime": {
            "path": path_hint(runtime_dir, root),
            "executable": executable.relative_to(runtime_dir).as_posix(),
            "files": runtime_files_before,
        },
        "runtimeVerificationFailures": runtime_verification_failures,
        "git": git_state(root),
        "budgetContract": budget_binding,
        "budgetEnforced": not args.pacing_only,
        "budgetDisplayContract": budget_display_contract() if not args.pacing_only else None,
        "basepath": basepath,
        "profile": args.profile,
        "benchmarkPreset": args.benchmark_preset,
        "renderApi": args.render_api,
        "dryRun": args.dry_run,
        "autoexecDelayMs": args.autoexec_delay_ms,
        "settleFrames": args.settle_frames,
        "mpClientDelayFrames": args.mp_client_delay_frames,
        "sampleFrames": args.sample_frames,
        "sampleMsec": args.sample_msec,
        "minPacingHz": args.min_pacing_hz,
        "maxP95Ms": args.max_p95_ms,
        "maxP99Ms": args.max_p99_ms,
        "maxFrameMs": args.max_frame_ms,
        "profileCvars": dict(PROFILE_DEFAULTS[args.profile].get("cvars", ())),
        "effectivePostMapCvars": effective_post_map_cvars(args.extra_cvars),
        "pbrFixtureAcceptanceRequired": args.pbr_fixture_acceptance_required,
        "pbrFixtureBinding": args.pbr_fixture_binding,
        "profileExecCommands": list(PROFILE_DEFAULTS[args.profile].get("execCommands", ())),
        "launchCvars": dict(args.launch_cvars),
        "execCommands": list(args.exec_commands),
    }
    report_json, report_md = write_reports(output_dir, results, metadata)
    print(f"wrote {report_md}")
    print(f"wrote {report_json}")

    if args.dry_run:
        return 0
    return 0 if all(result["status"] == "pass" for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
