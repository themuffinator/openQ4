#!/usr/bin/env python3
"""Run and report the openQ4 renderer validation matrix.

The default matrix is intentionally safe: it starts the staged client, runs
renderer self-tests and tier/startup probes, prints gfxInfo, then quits. Gameplay
map loads are listed in the generated report but are not launched unless a human
chooses to run them separately.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

VALIDATION_DIR = Path(__file__).resolve().parents[1] / "validation"
if str(VALIDATION_DIR) not in sys.path:
    sys.path.insert(0, str(VALIDATION_DIR))

from renderer_budget_contract import DEFAULT_CONTRACT_PATH, load_contract  # noqa: E402


SAFE_TIERS = ("auto", "legacy", "gl33", "gl41", "gl43", "gl45", "gl46")
PER_MAP_BUDGET_CONTRACT, PER_MAP_BUDGET_BINDING = load_contract(DEFAULT_CONTRACT_PATH)

# Keep in sync with MAX_CONSOLE_LINES in src/framework/Common.cpp. The engine
# silently ignores any "+command" beyond this limit, which would drop "+quit"
# and leave the case running until the timeout.
ENGINE_MAX_STARTUP_COMMANDS = 64
PBR_UNAVAILABLE_MARKER = "RendererPBRVisible self-test skipped: modern shader library unavailable"
CLUSTER_UNAVAILABLE_MARKER = "RendererClusterGrid self-test skipped: modern clustered lighting unavailable"

SELFTEST_CHECKS = [
    ["RendererModule self-test passed"],
    ["RendererContextLadder self-test passed"],
    ["RendererTierSelect self-test passed"],
    ["RendererTierContract self-test passed"],
    ["RendererUpload self-test passed"],
    ["RendererGpuTimer self-test passed", "RendererGpuTimer self-test skipped"],
    ["RendererScenePacket self-test passed"],
    ["RendererRenderGraph self-test passed"],
    ["RendererRenderGraphResource self-test passed", "RendererRenderGraphResource self-test skipped"],
    ["RendererMaterialResourceTable self-test passed", "RendererMaterialResourceTable self-test skipped"],
    ["RendererClassicGuiDomain self-test passed"],
    ["RendererClassicCinematicPostDomain self-test passed"],
    ["RendererClassicWorldAmbientDomain self-test passed"],
    ["RendererClassicInteractionDomain self-test passed"],
    ["RendererClassicFogBlendDomain self-test passed"],
    ["RendererClassicSubviewDomain self-test passed"],
    ["RendererClassicDeformDomain self-test passed"],
    ["RendererContracts self-test passed"],
    ["RendererGpuSkinning self-test passed"],
    ["RendererPBRMaterial self-test passed"],
    ["RendererGeometryResource self-test passed"],
    ["RendererGLStateCache self-test passed", "RendererGLStateCache self-test skipped"],
    ["RendererModernGLShaderLibrary self-test passed"],
    ["RendererModernGLDrawPlan self-test passed"],
    ["RendererModernGLSubmitPlan self-test passed"],
    ["RendererModernGLExecutor self-test passed"],
    ["RendererModernVisibility self-test passed"],
    ["RendererShadowPlanner self-test passed"],
]

STARTUP_CHECKS = [
    ["created OpenGL context"],
    ["Selected renderer tier:"],
    ["GL context profile:"],
    ["GL context request:"],
    ["Renderer caps:"],
    ["Renderer tier contract:"],
]

MANUAL_GAMEPLAY_MATRIX = [
    {
        "id": "sp-airdefense1",
        "mode": "SP",
        "map": "game/airdefense1",
        "purpose": "stock SP baseline, outdoor lighting and BSE smoke",
    },
    {
        "id": "sp-airdefense2",
        "mode": "SP",
        "map": "game/airdefense2",
        "purpose": "stock SP flashlight, projected shadows, animated characters",
    },
    {
        "id": "sp-storage2",
        "mode": "SP",
        "map": "game/storage2",
        "purpose": "indoor SP material and post-process coverage",
    },
    {
        "id": "sp-bse-heavy",
        "mode": "SP",
        "map": "game/medlabs",
        "purpose": "stress BSE effects while preserving stock assets",
    },
    {
        "id": "sp-cinematic-subview",
        "mode": "SP",
        "map": "game/mcc_landing",
        "purpose": "subviews, remote cameras, cinematic and GUI interaction",
    },
    {
        "id": "sp-mv2-ambient",
        "mode": "SP",
        "map": "maps/tools/mv2",
        "purpose": "controlled stock fixed-function world-ambient ownership and whole-view rollback",
    },
    {
        "id": "sp-mv2-interaction",
        "mode": "SP",
        "map": "maps/tools/mv2",
        "purpose": "controlled stock fixed-classic interaction and shadow ownership parity",
    },
    {
        "id": "sp-mv2-deform",
        "mode": "SP",
        "map": "maps/tools/mv2",
        "purpose": "controlled stock material-move deformation ownership and published-geometry parity",
    },
    {
        "id": "mp-q4dm1-listen",
        "mode": "MP",
        "map": "mp/q4dm1",
        "purpose": "listen-server and local-client MP renderer parity",
    },
]

DETERMINISTIC_CAPTURE_MATRIX = [
    {
        "id": "capture-startup-mainmenu",
        "mode": "SP",
        "scene": "main menu after logo skip",
        "purpose": "deterministic GUI composition, font/material atlas, and widescreen expansion",
    },
    {
        "id": "capture-renderer-visible-selftest",
        "mode": "safe startup",
        "scene": "rendererModernVisibleSelfTest",
        "purpose": "synthetic modern-visible depth/G-buffer/deferred/forward+/hybrid-scene/present composition with shadow-policy handoff",
    },
    {
        "id": "capture-renderer-compatibility-selftest",
        "mode": "safe startup",
        "scene": "rendererModernCompatibilitySelfTest",
        "purpose": "known fallback inventory for GUI/post/subview/render-demo/BSE categories",
    },
    {
        "id": "capture-sp-airdefense1-static",
        "mode": "SP",
        "scene": "game/airdefense1 fixed spawn, no input for 3 seconds",
        "purpose": "outdoor lighting, terrain decals, BSE smoke, and stock material parity",
    },
]

RENDERDOC_TIER_MATRIX = [
    {
        "tier": "gl33",
        "focus": "VAO/VBO/UBO baseline, graph resources, visible-depth/G-buffer/forward+ passes",
    },
    {
        "tier": "gl41",
        "focus": "macOS-class GLSL path and GL 4.1 context fallback behavior",
    },
    {
        "tier": "gl43",
        "focus": "SSBO scene records, compute validation dispatch, indirect-command generation",
    },
    {
        "tier": "gl45",
        "focus": "DSA texture/FBO updates, persistent upload defaults, and multi-bind groups",
    },
    {
        "tier": "gl46",
        "focus": "top-tier selection plus GL SPIR-V/bindless availability reporting without default use",
    },
]

SHADER_LIBRARY_TIER_MATRIX = [
    {
        "id": "shader-library-gl33",
        "tier": "gl33",
        "coverage": "GLSL 330 Shader Library V2 compile, link, exact-version lookup, and sampler reflection",
    },
    {
        "id": "shader-library-gl41",
        "tier": "gl41",
        "coverage": "GLSL 330/410 Shader Library V2 coverage for the macOS-class GL 4.1 portability floor",
    },
    {
        "id": "shader-library-gl43",
        "tier": "gl43",
        "coverage": "GLSL 330/410/430 Shader Library V2 coverage alongside GPU-driven SSBO-capable tiers",
    },
    {
        "id": "shader-library-gl45",
        "tier": "gl45",
        "coverage": "GLSL 330/410/430/450 Shader Library V2 coverage alongside low-overhead DSA-capable tiers",
    },
    {
        "id": "shader-library-gl46",
        "tier": "gl46",
        "coverage": "top-tier Shader Library V2 coverage with the highest selected GLSL variant and reflected sampler bindings",
    },
]

LONG_RUN_VALIDATION_MATRIX = [
    {
        "id": "longrun-vid-restart-10x",
        "mode": "SP",
        "purpose": "repeat `vid_restart` ten times under `r_glTier auto`, `gl33`, and the highest supported forced tier; inspect logs after each cycle",
    },
    {
        "id": "longrun-map-transition-sp",
        "mode": "SP",
        "purpose": "transition between `game/airdefense1`, `game/storage2`, and `game/medlabs` without restarting the process",
    },
    {
        "id": "longrun-mp-listen-reconnect",
        "mode": "MP",
        "purpose": "`mp/q4dm1` listen server with local client connect, disconnect, reconnect, then map restart",
    },
]

GAMEPLAY_BENCHMARK_HARNESS = [
    {
        "profile": "smoke",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile smoke",
        "coverage": "bounded SP gameplay smoke with screenshot, rendererBenchmarkCapture, framePacingSnapshot, gfxInfo, and zero-warning log gates",
    },
    {
        "profile": "required",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile required",
        "coverage": "all required SP maps plus the MP q4dm1 listen-server/local-client case using the selected tier and presentation settings",
    },
    {
        "profile": "campaign-split-state-transition",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile campaign-split-state-transition --timeout 360",
        "coverage": "real SP end-level target chain from game/mcc_2 through storage1 first, storage2, storage1 second, and game/tram1 with active map/filter assertions after each load",
    },
    {
        "profile": "world-ambient",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile world-ambient --pacing-only --no-gpu-timers",
        "coverage": "bordered stock maps/tools/mv2 capture with light, subview, GUI, post, portal-fade, and overlay islands disabled for shared world-ambient A/B ownership and explicit-blocker rollback evidence",
    },
    {
        "profile": "deform",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile deform --pacing-only --no-gpu-timers",
        "coverage": "bordered stock maps/tools/mv2 capture with the stock shaderDemos/move material override, fixed camera, shared world-ambient ownership, and explicit material-deform contract ownership",
    },
    {
        "profile": "interaction",
        "renderApi": "gl",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile interaction --render-api gl --pacing-only --no-gpu-timers --reference-dir .tmp\\renderer-references\\interaction\\gl\\classic\\savepaths --require-references --image-rms-threshold 0 --image-max-threshold 0 --difference-reference-dir .tmp\\renderer-references\\interaction\\gl\\shadows-off\\savepaths",
        "coverage": "OpenGL controlled unshadowed, stencil, projected-plus-point mapped, mixed mapped/stencil, and named map-admission fallback ownership with exact shared/classic TGA parity and a required shadows-on/off image delta",
    },
    {
        "profile": "interaction",
        "renderApi": "vk",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile interaction --render-api vk --pacing-only --no-gpu-timers --reference-dir .tmp\\renderer-references\\interaction\\vk\\classic\\savepaths --require-references --image-rms-threshold 0 --image-max-threshold 0 --difference-reference-dir .tmp\\renderer-references\\interaction\\vk\\shadows-off\\savepaths",
        "coverage": "Vulkan controlled unshadowed, stencil, projected-plus-point mapped, mixed mapped/stencil, and named map-admission fallback ownership with exact shared/classic TGA parity and a required shadows-on/off image delta",
    },
    {
        "profile": "interaction-shadow-stock",
        "renderApi": "gl",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile interaction-shadow-stock --render-api gl --pacing-only --no-gpu-timers --reference-dir .tmp\\renderer-references\\interaction-shadow-stock\\gl\\classic\\savepaths --require-references --image-rms-threshold 0 --image-max-threshold 0 --difference-reference-dir .tmp\\renderer-references\\interaction-shadow-stock\\gl\\shadows-off\\savepaths",
        "coverage": "OpenGL stock projected, point, CSM/parallel, dynamic, perforated, same-light hybrid, and exact translucent-moment fallback ownership with exact shared/classic TGA parity and required shadows-on/off image deltas",
    },
    {
        "profile": "interaction-shadow-stock",
        "renderApi": "vk",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile interaction-shadow-stock --render-api vk --pacing-only --no-gpu-timers --reference-dir .tmp\\renderer-references\\interaction-shadow-stock\\vk\\classic\\savepaths --require-references --image-rms-threshold 0 --image-max-threshold 0 --difference-reference-dir .tmp\\renderer-references\\interaction-shadow-stock\\vk\\shadows-off\\savepaths",
        "coverage": "Vulkan stock projected, point, CSM/parallel, dynamic, perforated, same-light hybrid, and exact translucent-moment fallback ownership with exact shared/classic TGA parity and required shadows-on/off image deltas",
    },
    {
        "profile": "tiers",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile tiers",
        "coverage": "forced auto/legacy/gl33/gl41/gl43/gl45/gl46 gameplay probes that either reach gameplay or fail closed with logged tier-contract reasons",
    },
    {
        "profile": "presentation",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile presentation --pacing-only",
        "coverage": "windowed/fullscreen coverage for r_swapInterval 0/1 and com_maxfps 0/120/240 while preserving uncapped high-refresh presentation behavior",
    },
    {
        "profile": "shadows",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile shadows",
        "coverage": "shadow-map correctness scenes with stencil, mapped, CSM, translucent, and debug-overlay/debug-mode presets",
    },
    {
        "profile": "shadow-regression",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile shadow-regression --reference-dir .tmp\\renderer-references\\shadow-regression\\windows-x64",
        "coverage": "bounded five-scene CSM-enabled projected, point, character/skinned, and alpha-tested shadow-map captures with optional TGA reference comparison and screenshot hashes",
    },
]

SHADOW_CORRECTNESS_MATRIX = [
    {
        "id": "shadow-projected-airdefense2",
        "mode": "SP",
        "map": "game/airdefense2",
        "purpose": "angled projected-light caster/receiver validation",
    },
    {
        "id": "shadow-point-storage2",
        "mode": "SP",
        "map": "game/storage2",
        "purpose": "point-light face coverage and local-light receiver validation",
    },
    {
        "id": "shadow-csm-airdefense1",
        "mode": "SP",
        "map": "game/airdefense1",
        "purpose": "CSM camera sweep readiness and outdoor directional coverage",
    },
    {
        "id": "shadow-cutout-storage2",
        "mode": "SP",
        "map": "game/storage2",
        "purpose": "hashed-alpha cutout fence/grate caster validation at distance",
    },
    {
        "id": "shadow-character-airdefense2",
        "mode": "SP",
        "map": "game/airdefense2",
        "purpose": "dynamic character shadow caster and receiver validation",
    },
    {
        "id": "shadow-translucent-medlabs",
        "mode": "SP",
        "map": "game/medlabs",
        "purpose": "optional translucent moment caster coverage where the selected tier supports it",
    },
]

HUMAN_REVIEW_CHECKLIST = [
    {
        "case": "sp-bse-heavy",
        "focus": "BSE-heavy effects in `game/medlabs`",
        "checks": "effect sprites/trails animate at the expected cadence, no black quads, no missing additive passes, no warning spam",
    },
    {
        "case": "sp-cinematic-subview",
        "focus": "cinematic/subview flow in `game/mcc_landing`",
        "checks": "remote-camera/subview content is visible, GUI overlays composite in the right order, cinematic handoff keeps frame pacing stable",
    },
    {
        "case": "mp-q4dm1-listen",
        "focus": "local MP listen server plus loopback client",
        "checks": "client reaches the map, player/world lighting matches host expectations, frame pacing remains uncapped when requested",
    },
]

PERF_REGRESSION_THRESHOLDS = [
    {
        "preset": "low",
        "p95Ms": 33,
        "p99Ms": 50,
        "budget": "75% screen-percentage experiment, 4x3x8 cluster-grid budget, 512 shadow-map budget, post quality 0",
    },
    {
        "preset": "baseline",
        "p95Ms": 20,
        "p99Ms": 28,
        "budget": "fixed 100% screen, 6x4x12 cluster-grid budget, 1024 shadow-map budget, post quality 1",
    },
    {
        "preset": "modern",
        "p95Ms": 16,
        "p99Ms": 24,
        "budget": "fixed 100% screen, 8x6x16 cluster-grid budget, 1024 shadow-map budget, post quality 2",
    },
    {
        "preset": "high-end",
        "p95Ms": 12,
        "p99Ms": 18,
        "budget": "fixed 100% screen, 8x6x16 cluster-grid budget, 2048 shadow-map budget, post quality 3",
    },
]

PROMOTION_EVIDENCE_REQUIRED_TOKENS = [
    "phase8=complete",
    "warnings=0",
    "visual=pass",
    "gameplay=pass",
    "renderdoc=pass",
    "perf=arb2-or-better",
    "presentation=pass",
    "rollback=pass",
    "debug=off",
]

PROMOTION_EVIDENCE_TOKEN = ";".join(PROMOTION_EVIDENCE_REQUIRED_TOKENS)

DEFAULT_PROMOTION_CRITERIA = [
    {
        "criterion": "tier",
        "required": "`r_glTier auto` selects a modern GL 3.3+ tier after driver quirks and compatibility gates are applied",
    },
    {
        "criterion": "renderer escape",
        "required": "`r_renderer best` leaves promotion available; explicit `r_renderer arb2` keeps the ARB2 bridge",
    },
    {
        "criterion": "compatibility gates",
        "required": "modern baseline features, UBOs, MRT, render graph, scene packets, and shader library readiness are available",
    },
    {
        "criterion": "fallback escape",
        "required": "the ARB2 compatibility bridge remains available for rollback and explicit user selection",
    },
    {
        "criterion": "conservative defaults",
        "required": "`r_renderer best` or explicit `r_renderer arb2` keeps ARB2 visible; shared classic domains, modern executor, submit, visible, side-path, debug, GPU-validation, bindless, shader-reload, and auto-promotion cvars remain off in a clean startup",
    },
    {
        "criterion": "validation evidence",
        "required": "`r_rendererPromotionEvidence` contains the Phase 8 evidence token after zero-warning deterministic visual checks, required SP/MP gameplay, RenderDoc tier captures, ARB2-or-better performance, presentation, rollback, and debug-off checks pass",
    },
    {
        "criterion": "manual sign-off",
        "required": "`r_rendererModernAutoPromote 1` is set only together with a complete `r_rendererPromotionEvidence` token",
    },
]

WARNING_PATTERNS = {
    "snPrintfOverflow": re.compile(r"idStr::snPrintf:\s*overflow", re.IGNORECASE),
    "idStrWarning": re.compile(r"WARNING:\s+idStr", re.IGNORECASE),
    "binaryImageCacheWrite": re.compile(
        r"WARNING:\s*(?:\^[0-9]\s*)*idBinaryImage:\s+Could not open (?:file|generated cache)\b",
        re.IGNORECASE,
    ),
    "expandedLoadscreenPublish": re.compile(
        r"WARNING:\s*(?:\^[0-9]\s*)*Could not publish expanded loading background\b",
        re.IGNORECASE,
    ),
    "shaderCompileOrLink": re.compile(r"(shader compile|program link).*(failed|error)|failed to compile", re.IGNORECASE),
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
    "vulkanCallFailed": re.compile(
        r"\bVulkan\b[^\r\n]{0,160}\bvk[A-Z][A-Za-z0-9_]*\b[^\r\n]{0,96}\bfailed\b",
        re.IGNORECASE,
    ),
    "fatal": re.compile(
        r"\bFatal Error\b|^[ \t]*(?:\*+[ \t]*)?FATAL[ \t]*:|(?:could not|unable to) initialize OpenGL",
        re.IGNORECASE | re.MULTILINE,
    ),
    "errorLine": re.compile(r"^[ \t]*(?:\*+[ \t]*)?ERROR(?:[ \t]*:|[ \t]*$)", re.MULTILINE),
}

MAX_FAILURE_DIAGNOSTICS = 32
MAX_FAILURE_DIAGNOSTIC_CHARS = 600


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def host_arch() -> str:
    machine = platform.machine().lower()
    if machine in ("amd64", "x86_64"):
        return "x64"
    if machine in ("arm64", "aarch64"):
        return "arm64"
    if machine in ("x86", "i386", "i686"):
        return "x86"
    return machine


def find_client_executable(root: Path) -> Path:
    install_dir = root / ".install"
    suffix = ".exe" if os.name == "nt" else ""
    candidate_prefixes = ("openQ4-client", "openQ4-client")
    for prefix in candidate_prefixes:
        preferred = install_dir / f"{prefix}_{host_arch()}{suffix}"
        if preferred.exists():
            return preferred

    candidates: list[Path] = []
    seen: set[Path] = set()
    for prefix in candidate_prefixes:
        for candidate in sorted(install_dir.glob(f"{prefix}_*{suffix}")):
            if candidate not in seen:
                candidates.append(candidate)
                seen.add(candidate)

    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    for candidate in candidates:
        if candidate.is_file():
            return candidate

    raise FileNotFoundError(f"openQ4 client executable not found under {install_dir}")


def default_basepath() -> str:
    if os.name == "nt":
        return r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"
    return ""


def sanitize_case_id(case_id: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", case_id)


def common_args(
    runtime_dir: Path,
    case_id: str,
    basepath: str,
    savepath: Path,
    skip_official_pak_validation: bool,
) -> list[str]:
    log_name = f"openq4_validation_{sanitize_case_id(case_id)}.log"
    args = [
        "+set",
        "win_allowMultipleInstances",
        "1",
        "+set",
        "logFile",
        "2",
        "+set",
        "logFileName",
        f"logs/{log_name}",
        "+set",
        "developer",
        "1",
        "+set",
        "r_ignoreGLErrors",
        "0",
        "+set",
        "r_fullscreen",
        "0",
        "+set",
        "g_autoScreenshot",
        "0",
        "+set",
        "r_glTier",
        "auto",
        "+set",
        "r_glDebugContext",
        "0",
        "+set",
        "r_rendererModernSubmit",
        "0",
        "+set",
        "r_rendererSharedSubview",
        "0",
        "+set",
        "r_rendererSharedInWorldGui",
        "0",
        "+set",
        "r_rendererSharedCinematicPost",
        "0",
        "+set",
        "r_rendererSharedSpecialFrame",
        "0",
        "+set",
        "fs_savepath",
        str(savepath),
        "+set",
        "fs_devpath",
        str(runtime_dir),
        "+set",
        "fs_game",
        "baseoq4",
    ]
    if skip_official_pak_validation:
        args += [
            "+set",
            "fs_validateOfficialPaks",
            "0",
            "+set",
            "g_allowAssetlessStartup",
            "1",
        ]
    if basepath:
        args += ["+set", "fs_basepath", basepath]
    return args


def build_safe_cases(tiers: tuple[str, ...]) -> list[dict[str, Any]]:
    selftest_commands = [
        "+set",
        "r_renderApi",
        "gl",
        "+set",
        "r_rendererMetrics",
        "2",
        "+set",
        "r_rendererModernExecutor",
        "1",
        "+set",
        "r_rendererModernSubmit",
        "0",
        # uiFontParitySelfTest asserts parity with the retail bitmap atlases, so
        # it has to run against them; the TrueType path is on by default and
        # deliberately rasterises its own glyphs and binds its own atlas.
        "+set",
        "r_useTrueTypeFonts",
        "0",
        "+rendererModuleSelfTest",
        "+rendererContextLadderSelfTest",
        "+rendererTierSelfTest",
        "+rendererTierContractSelfTest",
        "+uiFontParitySelfTest",
        "+rendererUploadSelfTest",
        "+rendererGpuTimerSelfTest",
        "+rendererScenePacketSelfTest",
        "+rendererRenderGraphSelfTest",
        "+rendererRenderGraphResourceSelfTest",
        "+rendererMaterialResourceTableSelfTest",
        "+rendererClassicGuiDomainSelfTest",
        "+rendererClassicCinematicPostDomainSelfTest",
        "+rendererClassicSpecialFrameDomainSelfTest",
        "+rendererClassicWorldAmbientDomainSelfTest",
        "+rendererClassicInteractionDomainSelfTest",
        "+rendererClassicFogBlendDomainSelfTest",
        "+rendererClassicSubviewDomainSelfTest",
        "+rendererClassicDeformDomainSelfTest",
        "+rendererContractsSelfTest",
        "+rendererGpuSkinningSelfTest",
        "+rendererPBRMaterialSelfTest",
        "+rendererGeometryResourceSelfTest",
        "+rendererGLStateCacheSelfTest",
        "+rendererModernGLExecutorSelfTest",
        "+rendererModernVisibilitySelfTest",
        "+rendererShadowPlannerSelfTest",
        "+gfxInfo",
    ]

    cases: list[dict[str, Any]] = [
        {
            "id": "renderer-foundation-selftests",
            "category": "selftest",
            "description": "Renderer foundation, shared contracts, GPU-animation, upload, metrics, packet, graph, material, geometry, shader, draw, submit, and executor self-tests.",
            "preservesConfig": True,
            "args": selftest_commands,
            "checks": SELFTEST_CHECKS + [["Selected renderer tier:"], ["GL context request:"], ["Renderer API: requested="]],
        },
        {
            "id": "renderer-visible-depth-selftest",
            "category": "selftest",
            "description": "Opt-in graph-backed visible modern depth and shadow-depth self-test.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererModernVisibleDepth",
                "1",
                "+rendererVisiblePathSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererVisiblePath self-test passed"],
                ["sceneDepth=1"],
                ["shadowMap=1"],
                ["overlay=1"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-gbuffer-selftest",
            "category": "selftest",
            "description": "Opt-in graph-backed opaque G-buffer self-test.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererModernOpaque",
                "1",
                "+rendererGBufferSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererGBuffer self-test passed"],
                ["mrt=1"],
                ["albedo=1"],
                ["normal=1"],
                ["material=1"],
                ["emissive=1"],
                ["overlay=1"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-pbr-visible-selftest",
            "category": "selftest",
            "description": "Guarded PBR material admission, sampler packing, and G-buffer/deferred/forward shader corridor self-test.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererModernOpaque",
                "1",
                "+rendererPBRVisibleSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererPBRVisible self-test passed", PBR_UNAVAILABLE_MARKER],
                ["gbuffer=1", PBR_UNAVAILABLE_MARKER],
                ["deferred=1", PBR_UNAVAILABLE_MARKER],
                ["forward=1", PBR_UNAVAILABLE_MARKER],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-light-image-atlas-selftest",
            "category": "selftest",
            "description": "Shared per-light falloff/projection atlas packing, residency, and rejection self-test.",
            "args": [
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+rendererLightImageAtlasSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererLightImageAtlas self-test passed"],
                ["Modern light image atlas:"],
            ],
        },
        {
            "id": "renderer-cluster-grid-selftest",
            "category": "selftest",
            "description": "Modern clustered light CPU binning and UBO fallback self-test.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererClusterDebug",
                "1",
                "+rendererClusterGridSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererClusterGrid self-test passed", CLUSTER_UNAVAILABLE_MARKER],
                ["lights=6", CLUSTER_UNAVAILABLE_MARKER],
                ["shadowDesc=", CLUSTER_UNAVAILABLE_MARKER],
                ["shadowBuffer=1", CLUSTER_UNAVAILABLE_MARKER],
                ["uploadedShadow=", CLUSTER_UNAVAILABLE_MARKER],
                ["overflow=", CLUSTER_UNAVAILABLE_MARKER],
                ["ubo=1", CLUSTER_UNAVAILABLE_MARKER],
                ["overlay=1", CLUSTER_UNAVAILABLE_MARKER],
                ["Modern clustered lighting:"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-shadow-planner-selftest",
            "category": "selftest",
            "description": "Modern shadow planner policy, budget, fallback, and clustered descriptor integration self-test.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererModernVisibleDepth",
                "1",
                "+set",
                "r_useShadowMap",
                "1",
                "+set",
                "r_shadowMapCSM",
                "1",
                "+rendererVisiblePathSelfTest",
                "+rendererShadowPlannerSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                # The trivial-pass escape (skippedLightShaderNoShadows) was
                # removed in phase 5c: self-test light materials are
                # synthesized to always cast shadows, so full coverage is
                # pinned unconditionally.
                ["RendererVisiblePath self-test passed"],
                ["ShadowMap caster admission self-test passed"],
                ["ShadowMap LOD admission self-test passed"],
                ["RendererShadowPlanner self-test passed"],
                ["RendererShadowPlanner regression coverage:"],
                ["projected=1"],
                ["point=1"],
                ["csm=1"],
                ["budgetFallback=1"],
                ["cacheReuse=1"],
                ["fairness=1"],
                ["throttleHistory=1"],
                ["casterAdmission=1"],
                ["receiverFallback=1"],
                ["arb2Parity="],
                ["projectedGate(on="],
                ["projectedTransform(pad="],
                ["sampleValidation(samples="],
                ["lod="],
                ["shadowMap=1"],
                ["projectedCSM="],
                ["mapped="],
                ["fallback="],
                ["skipped="],
                ["Modern shadow plan:"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-shadow-projected-diagnostic",
            "category": "selftest",
            "description": "Synthetic flashlight/projected-light diagnostic scene logging planner classification, ARB2 cascade/atlas/clip state, and receiver shader inputs.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererModernVisibleDepth",
                "1",
                "+set",
                "r_useShadowMap",
                "1",
                "+set",
                "r_shadowMapCSM",
                "1",
                "+set",
                "r_shadowMapProjectedCSM",
                "1",
                "+rendererVisiblePathSelfTest",
                "+rendererShadowProjectedDiagnosticSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                # Trivial-pass escape removed in phase 5c (see the planner
                # self-test case comment).
                ["RendererVisiblePath self-test passed"],
                ["SM projected-diagnostic scene=synthetic-flashlight"],
                ["SM projected-diagnostic fallbackValidation("],
                ["classification=projected"],
                ["planner(map=cascade"],
                ["arb2(cascades=3"],
                ["projectedTransform(pad="],
                ["sampleValidation(samples="],
                ["fallbackValidation(reason=mixed-w-signs"],
                ["clipPlane0="],
                ["atlas0="],
                ["split0="],
                ["SM projected-diagnostic receiverInputs("],
                ["RendererShadowProjectedDiagnostic self-test passed"],
                ["projectedGate="],
                ["projectedCSM="],
                ["Modern shadow plan:"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-deferred-resolve-selftest",
            "category": "selftest",
            "description": "Opt-in deferred-lite resolve over graph-backed G-buffer and clustered-light UBOs.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererModernDeferred",
                "1",
                "+set",
                "r_rendererModernDeferredDebug",
                "3",
                "+rendererDeferredResolveSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererDeferredResolve self-test passed"],
                ["program=1"],
                ["output=1"],
                ["resources=1"],
                ["cluster=1"],
                ["shadowTextures=1/1"],
                ["pixels="],
                ["reads="],
                ["overlay=1"],
                ["Modern GL executor:"],
                ["Modern shadow textures:"],
                ["Modern clustered lighting:"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-forward-plus-selftest",
            "category": "selftest",
            "description": "Opt-in clustered forward+ opaque, alpha-test, and transparent side-path self-test.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererForwardPlus",
                "1",
                "+rendererForwardPlusSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererForwardPlus self-test passed"],
                ["programs=1"],
                ["alphaProgram=1"],
                ["resources=1"],
                ["scene=1"],
                ["depth=1"],
                ["cluster=1"],
                ["shadowTextures=1/1"],
                ["draws="],
                ["opaque="],
                ["transparent="],
                ["reads="],
                ["Modern forward+:"],
                ["Modern shadow textures:"],
                ["modernForwardPlus req=1", "Modern forward+: cvar=1, req=1"],
                ["rendererMetrics forwardPlus(req=1", "Modern forward+: cvar=1, req=1"],
                ["Modern clustered lighting:"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-modern-visible-selftest",
            "category": "selftest",
            "description": "Opt-in hybrid visible-frame composition over modern depth, deferred-lite, forward+, HDR/post handoff, and present passes.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererModernVisible",
                "1",
                "+set",
                "r_useShadowMap",
                "1",
                "+set",
                "r_shadowMapCSM",
                "1",
                "+rendererModernVisibleSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererModernVisible self-test passed"],
                ["program=1"],
                ["resources=1"],
                ["source=1"],
                ["hybrid=1"],
                ["backBuffer=1"],
                ["shadow=1"],
                ["hdr="],
                ["postHandoff=1"],
                ["blocked=0"],
                ["composed=1"],
                ["copies=1"],
                ["postComposed=1"],
                ["depthCopies=1"],
                ["deferred=1", "deferred=0"],
                ["forward=1", "forward=0"],
                ["present=1"],
                ["Modern visible frame:"],
                # the self-test validates shadow readiness internally (shadow=1 in its
                # pass line); the post-test status line reads executor stats that the
                # self-test now resets on exit, so accept the clean state too
                ["shadowReady=1", "shadowReady=0"],
                ["shadow(mapped="],
                ["modernVisible req=1"],
                ["rendererMetrics modernVisible(req=1"],
                ["Modern forward+:"],
                ["Modern clustered lighting:"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-modern-compatibility-selftest",
            "category": "selftest",
            "description": "Phase 14 command-category ownership inventory with modern fullscreen GUI readiness and explicit post/subview/render-demo/BSE fallbacks.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererModernVisible",
                "1",
                "+rendererModernCompatibilitySelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererModernCompatibility self-test passed"],
                ["inventory="],
                ["gui=1/1"],
                ["post="],
                ["subview="],
                ["demo="],
                ["bse="],
                ["blocked=1"],
                ["Modern compatibility:"],
                ["modernCompatibility ready=1"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-compatibility-gates-selftest",
            "category": "selftest",
            "description": "Phase 15 driver-quirk table and fallback-gate coverage for missing UBO, broken MRT, missing timer query, missing buffer storage, and rejected debug context.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+rendererCompatibilityGatesSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererCompatibilityGates self-test passed"],
                ["Renderer driver quirks:"],
                ["Renderer compatibility gates:"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-default-promotion-selftest",
            "category": "selftest",
            "description": "Phase 8 evidence-gated default-promotion coverage for r_glTier auto, explicit ARB2 escapes, compatibility gates, legacy fallback availability, missing/incomplete/complete r_rendererPromotionEvidence, and auto-promote sign-off control.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+rendererDefaultPromotionSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererDefaultPromotion self-test passed"],
                ["Renderer default promotion:"],
                ["Renderer compatibility gates:"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-default-safety-selftest",
            "category": "selftest",
            "description": "Phase 13 conservative-default safety gate for ARB2 default visibility, rollback escape, shared classic domains, and default-off modern diagnostic side paths.",
            "args": [
                "+rendererDefaultSafetySelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererDefaultSafety self-test passed"],
                ["Renderer default safety:", "conservative=1", "sharedDeform=0", "rollback=available", "issues=none"],
                ["Renderer default promotion:", "active=0"],
                ["Renderer bootstrap:", "defaultVisible=ARB2", "sharedWorldFogBlend=0", "sharedSubview=0"],
            ],
        },
        {
            "id": "sdl3-wayland-window-lifecycle",
            "category": "windowing",
            "description": "native Wayland SDL3 window lifecycle smoke: windowed startup, fullscreen transition, windowed restore, compositor state refresh, and pixel-size diagnostics.",
            "videoDriver": "wayland",
            "assetless": True,
            "args": [
                "+set",
                "r_windowWidth",
                "960",
                "+set",
                "r_windowHeight",
                "540",
                "+set",
                "r_fullscreenDesktop",
                "1",
                "+set",
                "r_fullscreen",
                "1",
                "+vid_restart",
                "partial",
                "+set",
                "r_fullscreen",
                "0",
                "+set",
                "r_windowWidth",
                "800",
                "+set",
                "r_windowHeight",
                "600",
                "+vid_restart",
                "partial",
                "+gfxInfo",
            ],
            "checks": [
                ["SDL3: current video driver: wayland"],
                ["SDL3: native Wayland active"],
                ["SDL3: Wayland hints:"],
                ["SDL3: graphics bridge: OpenGL"],
                ["created OpenGL context"],
                ["SDL3: native Wayland window state after windowed change"],
                ["SDL3: native Wayland window state after fullscreen change"],
                ["pixels="],
                ["pixelDensity="],
                ["displayScale="],
                ["fullscreen=yes"],
                ["fullscreen=no"],
                ["Selected renderer tier:"],
                ["GL context request:"],
                ["Shutting down OpenGL subsystem (SDL3 backend)"],
            ],
        },
        {
            "id": "sdl3-wayland-window-stress",
            "category": "windowing",
            "description": "native Wayland SDL3 repeated window/fullscreen transition stress: multiple compositor-negotiated vid_restart cycles with changing window sizes.",
            "videoDriver": "wayland",
            "assetless": True,
            "args": [
                "+set",
                "r_windowWidth",
                "1024",
                "+set",
                "r_windowHeight",
                "576",
                "+set",
                "r_fullscreenDesktop",
                "1",
                "+set",
                "r_fullscreen",
                "1",
                "+vid_restart",
                "partial",
                "+set",
                "r_fullscreen",
                "0",
                "+set",
                "r_windowWidth",
                "832",
                "+set",
                "r_windowHeight",
                "624",
                "+vid_restart",
                "partial",
                "+set",
                "r_windowWidth",
                "1280",
                "+set",
                "r_windowHeight",
                "720",
                "+set",
                "r_fullscreen",
                "1",
                "+vid_restart",
                "partial",
                "+set",
                "r_fullscreen",
                "0",
                "+set",
                "r_windowWidth",
                "900",
                "+set",
                "r_windowHeight",
                "700",
                "+vid_restart",
                "partial",
                "+gfxInfo",
            ],
            "checks": [
                ["SDL3: current video driver: wayland"],
                ["SDL3: native Wayland active"],
                ["SDL3: Wayland hints:"],
                ["SDL3: native Wayland window state after windowed change"],
                ["SDL3: native Wayland window state after fullscreen change"],
                ["pixels="],
                ["pixelDensity="],
                ["displayScale="],
                ["fullscreen=yes"],
                ["fullscreen=no"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "sdl3-wayland-mouse-capture",
            "category": "input",
            "description": "native Wayland SDL3 relative mouse capture smoke: command-driven capture toggle, relative-mode confirmation, and release cleanup.",
            "videoDriver": "wayland",
            "assetless": True,
            "args": [
                "+sdl3MouseCaptureDiagnostics",
                "+gfxInfo",
            ],
            "checks": [
                ["SDL3: current video driver: wayland"],
                ["SDL3: native Wayland active"],
                ["SDL3 mouse capture diagnostics: begin"],
                ["SDL3 mouse capture diagnostics before:", "videoDriver=wayland", "nativeWayland=yes"],
                ["SDL3 mouse capture diagnostics after activate:", "relative=on", "captured=yes"],
                ["SDL3 mouse capture diagnostics after deactivate:", "relative=off", "captured=no"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "sdl3-wayland-mouse-capture-stress",
            "category": "input",
            "description": "native Wayland SDL3 repeated relative mouse capture stress: several activate/deactivate cycles to catch capture-state leaks.",
            "videoDriver": "wayland",
            "assetless": True,
            "args": [
                "+sdl3MouseCaptureDiagnostics",
                "4",
                "+gfxInfo",
            ],
            "checks": [
                ["SDL3: current video driver: wayland"],
                ["SDL3: native Wayland active"],
                ["SDL3 mouse capture diagnostics: begin repeat=4"],
                ["SDL3 mouse capture diagnostics: iteration 4/4"],
                ["SDL3 mouse capture diagnostics after activate:", "relative=on", "captured=yes"],
                ["SDL3 mouse capture diagnostics after deactivate:", "relative=off", "captured=no"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "sdl3-wayland-display-diagnostics",
            "category": "windowing",
            "description": "native Wayland SDL3 display diagnostics smoke: compositor display enumeration, scale/orientation reporting, selected-display resolution, and mode metadata.",
            "videoDriver": "wayland",
            "assetless": True,
            "args": [
                "+listDisplays",
                "+gfxInfo",
            ],
            "checks": [
                ["SDL3: current video driver: wayland"],
                ["SDL3: native Wayland active"],
                ["SDL3: detected"],
                ["display(s):"],
                ["contentScale"],
                ["orientation"],
                ["desktop"],
                ["current"],
                ["SDL3: r_screen ="],
                ["selected display"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "sdl3-x11-window-lifecycle",
            "category": "windowing",
            "description": "SDL3 X11/Xvfb window lifecycle smoke: windowed startup, fullscreen transition, windowed restore, renderer diagnostics, and clean SDL3 teardown.",
            "videoDriver": "x11",
            "assetless": True,
            "args": [
                "+set",
                "r_windowWidth",
                "960",
                "+set",
                "r_windowHeight",
                "540",
                "+set",
                "r_fullscreenDesktop",
                "1",
                "+set",
                "r_fullscreen",
                "1",
                "+vid_restart",
                "partial",
                "+set",
                "r_fullscreen",
                "0",
                "+set",
                "r_windowWidth",
                "800",
                "+set",
                "r_windowHeight",
                "600",
                "+vid_restart",
                "partial",
                "+gfxInfo",
            ],
            "checks": [
                ["SDL3: current video driver: x11"],
                ["SDL3: graphics bridge: OpenGL"],
                ["created OpenGL context"],
                ["MODE:"],
                ["Selected renderer tier:"],
                ["GL context request:"],
                ["Shutting down OpenGL subsystem (SDL3 backend)"],
            ],
        },
        {
            "id": "sdl3-x11-display-diagnostics",
            "category": "windowing",
            "description": "SDL3 X11/Xvfb fallback display diagnostics smoke: display enumeration, scale/orientation reporting, selected-display resolution, and mode metadata.",
            "videoDriver": "x11",
            "assetless": True,
            "args": [
                "+listDisplays",
                "+gfxInfo",
            ],
            "checks": [
                ["SDL3: current video driver: x11"],
                ["SDL3: detected"],
                ["display(s):"],
                ["contentScale"],
                ["orientation"],
                ["desktop"],
                ["current"],
                ["SDL3: r_screen ="],
                ["selected display"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "sdl3-force-x11-display-diagnostics",
            "category": "windowing",
            "description": "openQ4 XWayland fallback diagnostics smoke: OPENQ4_FORCE_X11 requests SDL's X11 driver and preserves display diagnostics.",
            "videoDriver": "x11",
            "assetless": True,
            "args": [
                "+listDisplays",
                "+gfxInfo",
            ],
            "checks": [
                ["SDL3: current video driver: x11"],
                ["OPENQ4_FORCE_X11=1"],
                ["SDL3: detected"],
                ["display(s):"],
                ["contentScale"],
                ["orientation"],
                ["desktop"],
                ["current"],
                ["SDL3: r_screen ="],
                ["selected display"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-benchmark-selftest",
            "category": "selftest",
            "description": "Phase 16 benchmark capture format, frame-time percentile, preset budget, and regression-threshold coverage.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+rendererBenchmarkSelfTest",
                "+rendererBenchmarkCapture",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererBenchmark self-test passed"],
                ["rendererBenchmark capture("],
                ["Renderer benchmark:"],
                ["Performance regression thresholds:"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-gpu-driven-selftest",
            "category": "selftest",
            "description": "GL 4.3 GPU-driven compute culling, compacted indirect command generation, CPU-reference validation, and masked multi-draw-indirect execution.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_glTier",
                "gl43",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererGpuValidation",
                "1",
                "+rendererGpuDrivenSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererGpuDriven self-test passed"],
                ["resources=1"],
                ["compute=1"],
                ["generated="],
                ["culled="],
                ["clusters="],
                ["mismatches=0"],
                ["readbacks=1"],
                ["indirect=1"],
                ["multiDraw="],
                ["dispatches="],
                ["rendererMetrics gpuDriven(req=1"],
                ["Modern GL executor:"],
                ["Modern visibility:"],
                ["gpuValidation=1"],
                ["Requested GL tier: gl43"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-low-overhead-selftest",
            "category": "selftest",
            "description": "GL 4.5 DSA resource allocation, persistent upload diagnostics, multi-bind texture/sampler groups, and low-overhead batch compaction.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_glTier",
                "gl45",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+rendererLowOverheadSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererLowOverhead self-test passed"],
                ["dsa=1"],
                ["multiBind=1"],
                ["textureDSA="],
                ["framebufferDSA="],
                ["textureMultiBind="],
                ["samplerMultiBind="],
                ["compactedBatches="],
                ["rendererMetrics lowOverhead(req=1"],
                ["Modern GL low-overhead:"],
                ["Renderer graph resources:"],
                ["Requested GL tier: gl45"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-vk-clear-startup",
            "category": "vulkan",
            "description": "Phase D Vulkan module startup: device + swapchain + GUI executor bring-up with validation layers on and zero validation-layer messages.",
            "assetless": True,
            "requiresVulkanModule": True,
            "preservesConfig": True,
            "args": [
                "+set",
                "r_renderApi",
                "vulkan",
                "+set",
                "r_vkValidation",
                "1",
                "+rendererContractsSelfTest",
                "+rendererGpuSkinningSelfTest",
                "+rendererClassicCinematicPostDomainSelfTest",
                "+rendererClassicSpecialFrameDomainSelfTest",
                "+rendererClassicWorldAmbientDomainSelfTest",
                "+rendererClassicInteractionDomainSelfTest",
                "+rendererClassicFogBlendDomainSelfTest",
                "+rendererClassicSubviewDomainSelfTest",
                "+rendererClassicDeformDomainSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["Renderer API: requested=vulkan active=vulkan disposition=module"],
                ["----- VK_InitRenderDevice -----"],
                ["Vulkan: created swapchain"],
                ["Vulkan: GUI executor initialized"],
                ["Vulkan renderer initialized"],
                ["RendererContracts self-test passed"],
                ["RendererGpuSkinning self-test passed"],
                ["RendererClassicCinematicPostDomain self-test passed"],
                ["RendererClassicSpecialFrameDomain self-test passed"],
                ["RendererClassicWorldAmbientDomain self-test passed"],
                ["RendererClassicInteractionDomain self-test passed"],
                ["RendererClassicFogBlendDomain self-test passed"],
                ["RendererClassicSubviewDomain self-test passed"],
                ["RendererClassicDeformDomain self-test passed"],
                ["GPU skinning:"],
            ],
        },
        {
            "id": "renderer-vk-fallback-drill",
            "category": "vulkan",
            "description": "Renderer-module break drill: the staged renderer-vk module is hidden for the run, so a vulkan request must fall back to the OpenGL module and report the fallback disposition.",
            "assetless": True,
            "requiresVulkanModule": True,
            "preservesConfig": True,
            "hideVkModule": True,
            "args": [
                "+set",
                "r_renderApi",
                "vulkan",
                "+gfxInfo",
            ],
            "checks": [
                ["Loading renderer module: api='vulkan'"],
                ["failed to load"],
                ["renderer API fallback: requested 'vulkan', active 'gl'"],
                ["Renderer API: requested=vulkan active=gl disposition=fallback"],
                ["Renderer API fallback reason:"],
                ["module load failed"],
                ["created OpenGL context"],
            ],
        },
    ]

    for shader_tier in SHADER_LIBRARY_TIER_MATRIX:
        tier = shader_tier["tier"]
        if tier not in tiers:
            continue
        cases.append(
            {
                "id": shader_tier["id"],
                "category": "shader-tier",
                "description": shader_tier["coverage"],
                "assetless": True,
                "args": [
                    "+set",
                    "r_rendererMetrics",
                    "2",
                    "+set",
                    "r_glTier",
                    tier,
                    "+set",
                    "r_rendererModernExecutor",
                    "1",
                    "+rendererShaderLibrarySelfTest",
                    "+gfxInfo",
                ],
                "checks": [
                    ["RendererModernGLShaderLibrary self-test passed"],
                    ["Modern GL shader library: available"],
                    ["programs="],
                    ["kinds="],
                    ["permutations="],
                    ["reflection(ubo="],
                    ["samplers="],
                    [f"Requested GL tier: {tier}"],
                    ["Selected renderer tier:"],
                    ["GL context request:"],
                ],
            }
        )

    for tier in tiers:
        case_args = [
            "+set",
            "r_glTier",
            tier,
            "+set",
            "r_rendererModernExecutor",
            "1" if tier not in ("legacy",) else "0",
            "+gfxInfo",
        ]
        cases.append(
            {
                "id": f"tier-{tier}",
                "category": "tier-startup",
                "description": f"Startup and gfxInfo probe for r_glTier {tier}.",
                "args": case_args,
                "checks": STARTUP_CHECKS + [[f"Requested GL tier: {tier}"]],
            }
        )

    cases += [
        {
            "id": "tier-gl33-debug-context",
            "category": "context-startup",
            "description": "Debug-context request path with non-debug fallback available in the ladder.",
            "args": [
                "+set",
                "r_glTier",
                "gl33",
                "+set",
                "r_glDebugContext",
                "1",
                "+gfxInfo",
            ],
            "checks": STARTUP_CHECKS + [["Requested GL tier: gl33"], ["requestedDebug=1"]],
        },
        {
            "id": "present-vsync0-fps0",
            "category": "presentation-startup",
            "description": "Unlocked presentation startup probe.",
            "args": [
                "+set",
                "r_swapInterval",
                "0",
                "+set",
                "com_maxfps",
                "0",
                "+gfxInfo",
            ],
            "checks": STARTUP_CHECKS,
        },
        {
            "id": "present-vsync1-fps240",
            "category": "presentation-startup",
            "description": "High-refresh capped presentation startup probe.",
            "args": [
                "+set",
                "r_swapInterval",
                "1",
                "+set",
                "com_maxfps",
                "240",
                "+gfxInfo",
            ],
            "checks": STARTUP_CHECKS,
        },
        {
            "id": "present-vsync1-fps120",
            "category": "presentation-startup",
            "description": "120 FPS capped presentation startup probe.",
            "args": [
                "+set",
                "r_swapInterval",
                "1",
                "+set",
                "com_maxfps",
                "120",
                "+gfxInfo",
            ],
            "checks": STARTUP_CHECKS,
        },
    ]

    if "gl43" not in tiers:
        cases = [case for case in cases if case["id"] != "renderer-gpu-driven-selftest"]
    if "gl45" not in tiers:
        cases = [case for case in cases if case["id"] != "renderer-low-overhead-selftest"]
    if "gl33" not in tiers:
        cases = [case for case in cases if case["id"] != "tier-gl33-debug-context"]

    return cases


def requested_sdl_video_driver() -> str:
    for name in ("SDL_VIDEO_DRIVER", "SDL_VIDEODRIVER"):
        value = os.environ.get(name, "").strip().lower()
        if value:
            return value
    return ""


def filter_driver_specific_cases(cases: list[dict[str, Any]]) -> list[dict[str, Any]]:
    video_driver = requested_sdl_video_driver()
    return [
        case
        for case in cases
        if not case.get("videoDriver") or case.get("videoDriver") == video_driver
    ]


def vk_module_path(runtime_dir: Path) -> Path:
    if os.name == "nt":
        suffix = ".dll"
    elif sys.platform == "darwin":
        suffix = ".dylib"
    else:
        suffix = ".so"
    return runtime_dir / f"renderer-vk_{host_arch()}{suffix}"


def filter_vulkan_module_cases(cases: list[dict[str, Any]], runtime_dir: Path) -> list[dict[str, Any]]:
    # the Vulkan cases need a staged renderer-vk module and a live Vulkan
    # driver. Headless Linux legs (Xvfb/WSL) offer neither, so they stay
    # dropped there. Windows has a native driver; macOS runs the module on
    # MoltenVK, which is bundled with the package, so both hosts qualify once
    # the module is staged next to the executable.
    if (os.name == "nt" or sys.platform == "darwin") and vk_module_path(runtime_dir).exists():
        return cases
    dropped = [case["id"] for case in cases if case.get("requiresVulkanModule")]
    if dropped:
        print(f"note: skipping Vulkan module cases (module not staged or unsupported host): {', '.join(dropped)}")
    return [case for case in cases if not case.get("requiresVulkanModule")]


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


def count_warning_signatures(text: str) -> dict[str, int]:
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


def format_warning_signatures(warnings: dict[str, int]) -> str:
    active = [f"{name}={count}" for name, count in sorted(warnings.items()) if count > 0]
    return ", ".join(active) if active else "0"


def evaluate_checks(text: str, checks: list[list[str]], warnings: dict[str, int]) -> tuple[bool, list[str]]:
    missing: list[str] = []
    for alternatives in checks:
        if not any(pattern in text for pattern in alternatives):
            missing.append(" or ".join(alternatives))
    failed_markers = ["self-test failed"]
    for marker in failed_markers:
        if marker in text:
            missing.append(f"unexpected marker: {marker}")
    missing += [f"warning signature: {name}={count}" for name, count in sorted(warnings.items()) if count > 0]
    return len(missing) == 0, missing


def extract_summary(text: str) -> dict[str, str]:
    summary: dict[str, str] = {}
    for key, pattern in {
        "context": r"created OpenGL context ([^\r\n]+)",
        "selectedTier": r"Selected renderer tier:\s*([^\r\n]+)",
        "contextProfile": r"GL context profile:\s*([^\r\n]+)",
        "contextRequest": r"GL context request:\s*([^\r\n]+)",
    }.items():
        match = re.search(pattern, text)
        if match:
            summary[key] = match.group(1).strip()
    return summary


def print_failure_details(result: dict[str, Any]) -> None:
    print(f"  exitCode={result['exitCode']} timedOut={int(result['timedOut'])}")
    if result["log"]:
        print(f"  log: {result['log']}")
    print(f"  stdout: {result['stdout']}")
    print(f"  stderr: {result['stderr']}")
    if result["missing"]:
        print("  missing:")
        for missing in result["missing"]:
            print(f"    - {missing}")
    if result.get("failureDiagnostics"):
        print("  matched diagnostics:")
        for diagnostic in result["failureDiagnostics"]:
            print(f"    - {format_failure_diagnostic(diagnostic)}")
        omitted = result.get("failureDiagnosticsOmitted", 0)
        if omitted:
            print(f"    ... {omitted} additional matching line(s) omitted")
    tail_source = result["log"] or result["stdout"]
    if tail_source:
        tail_path = Path(tail_source)
        if tail_path.is_file():
            lines = tail_path.read_text(encoding="utf-8", errors="replace").splitlines()
            if lines:
                print("  tail:")
                for line in lines[-25:]:
                    print(f"    {line}")


def run_case(
    root: Path,
    executable: Path,
    runtime_dir: Path,
    output_dir: Path,
    savepath: Path,
    basepath: str,
    case: dict[str, Any],
    timeout_seconds: int,
    skip_official_pak_validation: bool,
) -> dict[str, Any]:
    case_id = case["id"]
    log_name = f"openq4_validation_{sanitize_case_id(case_id)}.log"
    stdout_path = output_dir / f"{sanitize_case_id(case_id)}.out.txt"
    stderr_path = output_dir / f"{sanitize_case_id(case_id)}.err.txt"
    log_path_guess = find_log(savepath, log_name)
    if log_path_guess is not None:
        log_path_guess.unlink()

    case_assetless = bool(case.get("assetless", False))
    case_basepath = "" if case_assetless else basepath
    case_skip_official_pak_validation = skip_official_pak_validation or case_assetless
    args = common_args(runtime_dir, case_id, case_basepath, savepath, case_skip_official_pak_validation) + case["args"] + ["+quit"]
    startup_commands = sum(1 for arg in args if arg.startswith("+"))
    if startup_commands > ENGINE_MAX_STARTUP_COMMANDS:
        raise RuntimeError(
            f"case {case_id} passes {startup_commands} '+' startup commands; the engine keeps only the "
            f"first {ENGINE_MAX_STARTUP_COMMANDS} (MAX_CONSOLE_LINES) and would silently drop '+quit'"
        )
    # drill lever: hide the staged renderer-vk module so the loader's
    # fallback ladder is exercised for real, restoring it afterwards
    module_path = vk_module_path(runtime_dir)
    hidden_module_path = module_path.with_name(module_path.name + ".drill-hidden")
    hide_vk_module = bool(case.get("hideVkModule", False))
    # cvars set on the command line are archived on exit; cases that opt
    # renderer selection cvars in must not leak them into later cases or
    # the user's config
    config_path = savepath / "baseoq4" / "openQ4Config.cfg"
    preserve_config = bool(case.get("preservesConfig", False))
    saved_config = config_path.read_bytes() if preserve_config and config_path.exists() else None

    started = time.time()
    timed_out = False
    if hide_vk_module:
        module_path.rename(hidden_module_path)
    try:
        with stdout_path.open("w", encoding="utf-8", errors="replace") as stdout_file, stderr_path.open("w", encoding="utf-8", errors="replace") as stderr_file:
            process = subprocess.Popen(
                [str(executable)] + args,
                cwd=str(runtime_dir),
                stdout=stdout_file,
                stderr=stderr_file,
            )
            try:
                exit_code = process.wait(timeout=timeout_seconds)
            except subprocess.TimeoutExpired:
                timed_out = True
                process.kill()
                exit_code = process.wait(timeout=10)
    finally:
        if hide_vk_module and hidden_module_path.exists():
            hidden_module_path.rename(module_path)
        if preserve_config:
            if saved_config is None:
                config_path.unlink(missing_ok=True)
            else:
                config_path.write_bytes(saved_config)

    elapsed = time.time() - started
    log_path = find_log(savepath, log_name)
    log_text = ""
    case_log_path = output_dir / f"{sanitize_case_id(case_id)}.log"
    if log_path is not None:
        log_text = log_path.read_text(encoding="utf-8", errors="replace")
        case_log_path.write_text(log_text, encoding="utf-8")
    stdout_text = stdout_path.read_text(encoding="utf-8", errors="replace") if stdout_path.exists() else ""
    stderr_text = stderr_path.read_text(encoding="utf-8", errors="replace") if stderr_path.exists() else ""
    diagnostic_sources = (
        ("log", log_text),
        ("stdout", stdout_text),
        ("stderr", stderr_text),
    )
    diagnostic_text = "\n".join(part for _, part in diagnostic_sources if part)

    warning_signatures = count_warning_signatures(diagnostic_text)
    failure_diagnostics, failure_diagnostics_omitted = collect_failure_diagnostics(diagnostic_sources)
    checks_ok, missing = evaluate_checks(diagnostic_text, case["checks"], warning_signatures)
    ok = exit_code == 0 and not timed_out and log_path is not None and checks_ok
    return {
        "id": case_id,
        "category": case["category"],
        "description": case["description"],
        "assetless": case_assetless,
        "status": "pass" if ok else "fail",
        "exitCode": exit_code,
        "timedOut": timed_out,
        "elapsedSeconds": round(elapsed, 2),
        "log": str(case_log_path) if log_path is not None else "",
        "stdout": str(stdout_path),
        "stderr": str(stderr_path),
        "missing": missing,
        "warningSignatures": warning_signatures,
        "failureDiagnostics": failure_diagnostics,
        "failureDiagnosticsOmitted": failure_diagnostics_omitted,
        "summary": extract_summary(diagnostic_text),
    }


def write_reports(output_dir: Path, results: list[dict[str, Any]], metadata: dict[str, Any]) -> tuple[Path, Path]:
    report_json = output_dir / "renderer_validation_report.json"
    report_md = output_dir / "renderer_validation_report.md"

    payload = {
        "metadata": metadata,
        "results": results,
        "manualGameplayMatrix": MANUAL_GAMEPLAY_MATRIX,
        "gameplayBenchmarkHarness": GAMEPLAY_BENCHMARK_HARNESS,
        "shadowCorrectnessMatrix": SHADOW_CORRECTNESS_MATRIX,
        "humanReviewChecklist": HUMAN_REVIEW_CHECKLIST,
        "deterministicCaptureMatrix": DETERMINISTIC_CAPTURE_MATRIX,
        "renderDocTierMatrix": RENDERDOC_TIER_MATRIX,
        "shaderLibraryTierMatrix": SHADER_LIBRARY_TIER_MATRIX,
        "longRunValidationMatrix": LONG_RUN_VALIDATION_MATRIX,
        "perfRegressionThresholds": PERF_REGRESSION_THRESHOLDS,
        "perMapBudgetContract": PER_MAP_BUDGET_BINDING,
        "perMapBudgets": PER_MAP_BUDGET_CONTRACT["budgets"],
        "promotionEvidenceGate": {
            "cvar": "r_rendererPromotionEvidence",
            "requiredTokens": PROMOTION_EVIDENCE_REQUIRED_TOKENS,
            "completeToken": PROMOTION_EVIDENCE_TOKEN,
            "autoPromoteCvar": "r_rendererModernAutoPromote",
            "status": "blocked-until-manual-evidence",
        },
        "defaultPromotionCriteria": DEFAULT_PROMOTION_CRITERIA,
    }
    report_json.write_text(json.dumps(payload, indent=2), encoding="utf-8")

    passed = sum(1 for result in results if result["status"] == "pass")
    failed = sum(1 for result in results if result["status"] != "pass")
    lines = [
        "# Renderer Validation Matrix Report",
        "",
        f"- Generated: {metadata['generated']}",
        f"- Host: {metadata['host']}",
        f"- Executable: `{metadata['executable']}`",
        f"- Save path: `{metadata['savepath']}`",
        f"- Base path: `{metadata['basepath'] or 'not set'}`",
        f"- Automated cases: {passed} passed, {failed} failed",
        "",
        "## Automated Safe Cases",
        "",
        "| Status | Case | Category | Context | Selected Tier | Warning Signatures | Log |",
        "|---|---|---|---|---|---|---|",
    ]
    for result in results:
        summary = result["summary"]
        context = summary.get("context", summary.get("contextProfile", ""))
        selected = summary.get("selectedTier", "")
        warnings = format_warning_signatures(result.get("warningSignatures", {}))
        log = result["log"] or result["stdout"]
        lines.append(
            f"| {result['status']} | `{result['id']}` | {result['category']} | {context} | {selected} | {warnings} | `{log}` |"
        )
        if result["missing"]:
            lines.append(f"|  | missing |  | {'; '.join(result['missing'])} |  |  |  |")

    diagnostic_results = [result for result in results if result.get("failureDiagnostics")]
    if diagnostic_results:
        lines += [
            "",
            "## Matched Failure Diagnostics",
            "",
            "The exact matched lines are retained here even when they fall outside the normal log tail.",
        ]
        for result in diagnostic_results:
            lines += [
                "",
                f"### `{result['id']}`",
                "",
                "```text",
            ]
            lines.extend(format_failure_diagnostic(item) for item in result["failureDiagnostics"])
            omitted = result.get("failureDiagnosticsOmitted", 0)
            if omitted:
                lines.append(f"... {omitted} additional matching line(s) omitted")
            lines.append("```")

    lines += [
        "",
        "## Manual Gameplay Matrix",
        "",
        "These cases are required for renderer release sign-off, but this runner does not launch them by default because map startup is currently freeze-prone in local validation.",
        "",
        "| Case | Mode | Map | Purpose |",
        "|---|---|---|---|",
    ]
    for manual in MANUAL_GAMEPLAY_MATRIX:
        lines.append(f"| `{manual['id']}` | {manual['mode']} | `{manual['map']}` | {manual['purpose']} |")

    lines += [
        "",
        "## Gameplay Benchmark Harness",
        "",
        "`tools/tests/renderer_gameplay_benchmark.py` is the opt-in map-loading runner for Phase 12 evidence. It launches from `.install`, enters SP maps or an MP listen server plus loopback client, waits for streaming, records `rendererBenchmarkCapture`, captures screenshots, optionally compares TGA references, and fails on renderer, Vulkan validation/call, fatal, and engine ERROR diagnostics.",
        "",
        "| Profile | Command | Coverage |",
        "|---|---|---|",
    ]
    for item in GAMEPLAY_BENCHMARK_HARNESS:
        lines.append(f"| `{item['profile']}` | `{item['command']}` | {item['coverage']} |")

    lines += [
        "",
        "## Shadow Correctness Matrix",
        "",
        "| Case | Mode | Map | Purpose |",
        "|---|---|---|---|",
    ]
    for item in SHADOW_CORRECTNESS_MATRIX:
        lines.append(f"| `{item['id']}` | {item['mode']} | `{item['map']}` | {item['purpose']} |")

    lines += [
        "",
        "## Human Review Checklist",
        "",
        "| Case | Focus | Checks |",
        "|---|---|---|",
    ]
    for item in HUMAN_REVIEW_CHECKLIST:
        lines.append(f"| `{item['case']}` | {item['focus']} | {item['checks']} |")

    lines += [
        "",
        "## Deterministic Capture Matrix",
        "",
        "| Case | Mode | Scene | Purpose |",
        "|---|---|---|---|",
    ]
    for capture in DETERMINISTIC_CAPTURE_MATRIX:
        lines.append(f"| `{capture['id']}` | {capture['mode']} | {capture['scene']} | {capture['purpose']} |")

    lines += [
        "",
        "## RenderDoc Tier Matrix",
        "",
        "| Forced Tier | Capture Focus |",
        "|---|---|",
    ]
    for item in RENDERDOC_TIER_MATRIX:
        lines.append(f"| `r_glTier {item['tier']}` | {item['focus']} |")

    lines += [
        "",
        "## Shader Library Tier Matrix",
        "",
        "| Case | Forced Tier | Coverage |",
        "|---|---|---|",
    ]
    for item in SHADER_LIBRARY_TIER_MATRIX:
        lines.append(f"| `{item['id']}` | `r_glTier {item['tier']}` | {item['coverage']} |")

    lines += [
        "",
        "## Long-Run Matrix",
        "",
        "| Case | Mode | Purpose |",
        "|---|---|---|",
    ]
    for item in LONG_RUN_VALIDATION_MATRIX:
        lines.append(f"| `{item['id']}` | {item['mode']} | {item['purpose']} |")

    lines += [
        "",
        "## Performance Regression Thresholds",
        "",
        "| Preset | P95 Budget | P99 Budget | Budget Shape |",
        "|---|---:|---:|---|",
    ]
    for item in PERF_REGRESSION_THRESHOLDS:
        lines.append(f"| `{item['preset']}` | {item['p95Ms']} ms | {item['p99Ms']} ms | {item['budget']} |")

    lines += [
        "",
        "## Per-Map CPU/GPU Budget Contract",
        "",
        f"Contract `{PER_MAP_BUDGET_BINDING['contractId']}` / `{PER_MAP_BUDGET_BINDING['sha256']}` uses integer microseconds and exact map/backend/profile selection. These are target ceilings; measured values are retained by the gameplay and stock evidence reports.",
        "",
        "| Budget | Map | Backend | Profile | Minimum CPU/GPU samples | CPU P95/P99 | GPU P95/P99 |",
        "|---|---|---|---|---:|---:|---:|",
    ]
    for item in PER_MAP_BUDGET_CONTRACT["budgets"]:
        lines.append(
            f"| `{item['id']}` | `{item['map']}` | `{item['backend']}` | `{item['profile']}` | "
            f"{item['minimumSamples']['cpu']}/{item['minimumSamples']['gpu']} | "
            f"{item['cpu']['p95Us']}/{item['cpu']['p99Us']} us | "
            f"{item['gpu']['p95Us']}/{item['gpu']['p99Us']} us |"
        )

    lines += [
        "",
        "## Promotion Evidence Gate",
        "",
        "`r_rendererModernAutoPromote 1` is ignored by the engine unless `r_rendererPromotionEvidence` carries a complete Phase 8 evidence token.",
        "",
        "Required token:",
        "",
        f"`{PROMOTION_EVIDENCE_TOKEN}`",
        "",
        "| Token | Meaning |",
        "|---|---|",
        "| `phase8=complete` | The Phase 8 evidence bundle has been reviewed as a single promotion candidate. |",
        "| `warnings=0` | Renderer validation, gameplay, and benchmark logs are free of renderer warning/fatal/signature failures. |",
        "| `visual=pass` | Deterministic captures and human visual checks pass for materials, characters, GUI, post, fog/blend, BSE, and shadow cases. |",
        "| `gameplay=pass` | Required SP maps and the MP q4dm1 listen/local-client case reach gameplay and pass log/screenshot gates. |",
        "| `renderdoc=pass` | Required GL-tier RenderDoc captures show named resources and expected pass contents. |",
        "| `perf=arb2-or-better` | Modern candidate P95/P99 frame time is ARB2-or-better for target scenes and presets. |",
        "| `presentation=pass` | High-refresh and vsync presentation cases preserve uncapped rendering with 60 Hz simulation. |",
        "| `rollback=pass` | `r_renderer arb2`, `r_glTier legacy`, and modern-disable rollback commands work after modern-visible frames. |",
        "| `debug=off` | Promotion does not depend on debug-only overlays, validation readbacks, bindless experiments, or shader reload. |",
    ]

    lines += [
        "",
        "## Default Promotion Criteria",
        "",
        "| Criterion | Required Evidence |",
        "|---|---|",
    ]
    for item in DEFAULT_PROMOTION_CRITERIA:
        lines.append(f"| {item['criterion']} | {item['required']} |")

    report_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return report_json, report_md


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tiers", default=",".join(SAFE_TIERS), help="Comma-separated r_glTier startup probes.")
    parser.add_argument("--cases", default="", help="Comma-separated automated safe case ids to run. Defaults to all cases.")
    parser.add_argument("--timeout", type=int, default=60, help="Per-case timeout in seconds.")
    parser.add_argument("--basepath", default=default_basepath(), help="Quake 4 install/base path. Omit or set empty to skip fs_basepath.")
    parser.add_argument(
        "--savepath",
        default="",
        help="Save path root. Defaults to <output-dir>/savepath for an isolated run.",
    )
    parser.add_argument("--output-dir", default="", help="Report/output directory. Defaults to <repo>/.tmp/renderer-validation/<timestamp>.")
    parser.add_argument(
        "--executable",
        default="",
        help="Explicit client or launcher to test. Defaults to the host-matching staged client.",
    )
    parser.add_argument(
        "--runtime-dir",
        default="",
        help="Runtime root for working directory, renderer modules, and fs_devpath. Defaults to <repo>/.install.",
    )
    parser.add_argument(
        "--skip-official-pak-validation",
        action="store_true",
        help="Disable official q4base PK4 validation for assetless engine-startup smoke checks.",
    )
    parser.add_argument("--list", action="store_true", help="List automated and manual cases without running them.")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    root = repo_root()
    tiers = tuple(tier.strip() for tier in args.tiers.split(",") if tier.strip())
    safe_cases = build_safe_cases(tiers)
    requested_cases = tuple(case_id.strip() for case_id in args.cases.split(",") if case_id.strip())
    if requested_cases:
        available = {case["id"] for case in safe_cases}
        missing_cases = sorted(case_id for case_id in requested_cases if case_id not in available)
        if missing_cases:
            print(f"Unknown automated safe case id(s): {', '.join(missing_cases)}", file=sys.stderr)
            print("Use --list to see available case ids.", file=sys.stderr)
            return 2
        requested = set(requested_cases)
        safe_cases = [case for case in safe_cases if case["id"] in requested]
    if args.list:
        print("Automated safe cases:")
        for case in safe_cases:
            print(f"  {case['id']}: {case['description']}")
        print("\nManual gameplay cases:")
        for case in MANUAL_GAMEPLAY_MATRIX:
            print(f"  {case['id']}: {case['mode']} {case['map']} - {case['purpose']}")
        print("\nGameplay benchmark harness profiles:")
        for case in GAMEPLAY_BENCHMARK_HARNESS:
            print(f"  {case['profile']}: {case['command']} - {case['coverage']}")
        print("\nShadow correctness cases:")
        for case in SHADOW_CORRECTNESS_MATRIX:
            print(f"  {case['id']}: {case['mode']} {case['map']} - {case['purpose']}")
        print("\nHuman review checklist:")
        for case in HUMAN_REVIEW_CHECKLIST:
            print(f"  {case['case']}: {case['focus']} - {case['checks']}")
        print("\nDeterministic capture cases:")
        for case in DETERMINISTIC_CAPTURE_MATRIX:
            print(f"  {case['id']}: {case['mode']} {case['scene']} - {case['purpose']}")
        print("\nRenderDoc tier cases:")
        for case in RENDERDOC_TIER_MATRIX:
            print(f"  r_glTier {case['tier']}: {case['focus']}")
        print("\nShader library tier cases:")
        for case in SHADER_LIBRARY_TIER_MATRIX:
            print(f"  {case['id']}: r_glTier {case['tier']} - {case['coverage']}")
        print("\nLong-run cases:")
        for case in LONG_RUN_VALIDATION_MATRIX:
            print(f"  {case['id']}: {case['mode']} - {case['purpose']}")
        print("\nPerformance regression thresholds:")
        for item in PERF_REGRESSION_THRESHOLDS:
            print(f"  {item['preset']}: P95 <= {item['p95Ms']} ms, P99 <= {item['p99Ms']} ms - {item['budget']}")
        print(
            "\nPer-map CPU/GPU budget contract: "
            f"{PER_MAP_BUDGET_BINDING['contractId']} ({PER_MAP_BUDGET_BINDING['sha256']})"
        )
        for item in PER_MAP_BUDGET_CONTRACT["budgets"]:
            print(
                f"  {item['id']}: {item['map']} {item['backend']} {item['profile']} "
                f"CPU {item['cpu']['p95Us']}/{item['cpu']['p99Us']} us, "
                f"GPU {item['gpu']['p95Us']}/{item['gpu']['p99Us']} us"
            )
        print("\nDefault promotion criteria:")
        for item in DEFAULT_PROMOTION_CRITERIA:
            print(f"  {item['criterion']}: {item['required']}")
        return 0

    if args.executable:
        executable = Path(args.executable).resolve()
        if not executable.is_file():
            print(f"explicit client executable does not exist: {executable}", file=sys.stderr)
            return 2
        if os.name != "nt" and not os.access(executable, os.X_OK):
            print(f"explicit client executable is not executable: {executable}", file=sys.stderr)
            return 2
    else:
        executable = find_client_executable(root)
    runtime_dir = Path(args.runtime_dir).resolve() if args.runtime_dir else root / ".install"
    if not runtime_dir.is_dir():
        print(f"runtime directory does not exist: {runtime_dir}", file=sys.stderr)
        return 2
    if not requested_cases:
        safe_cases = filter_driver_specific_cases(safe_cases)
        safe_cases = filter_vulkan_module_cases(safe_cases, runtime_dir)
    timestamp = time.strftime("%Y%m%d-%H%M%S")
    output_dir = Path(args.output_dir).resolve() if args.output_dir else root / ".tmp" / "renderer-validation" / timestamp
    output_dir.mkdir(parents=True, exist_ok=True)
    savepath = Path(args.savepath).resolve() if args.savepath else output_dir / "savepath"
    savepath.mkdir(parents=True, exist_ok=True)

    basepath = args.basepath
    if basepath and not Path(basepath).exists():
        print(f"warning: basepath does not exist, omitting fs_basepath: {basepath}", file=sys.stderr)
        basepath = ""

    results = []
    for case in safe_cases:
        print(f"running {case['id']}...")
        result = run_case(
            root,
            executable,
            runtime_dir,
            output_dir,
            savepath,
            basepath,
            case,
            args.timeout,
            args.skip_official_pak_validation,
        )
        print(f"  {result['status']} ({result['elapsedSeconds']}s)")
        if result["status"] != "pass":
            print_failure_details(result)
        results.append(result)

    metadata = {
        "generated": time.strftime("%Y-%m-%d %H:%M:%S %z"),
        "host": f"{platform.system()} {platform.release()} {platform.machine()}",
        "executable": str(executable),
        "runtimeDir": str(runtime_dir),
        "savepath": str(savepath),
        "basepath": basepath,
        "skipOfficialPakValidation": args.skip_official_pak_validation,
    }
    report_json, report_md = write_reports(output_dir, results, metadata)
    print(f"wrote {report_md}")
    print(f"wrote {report_json}")

    return 0 if all(result["status"] == "pass" for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
