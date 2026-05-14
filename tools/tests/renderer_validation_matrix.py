#!/usr/bin/env python3
"""Run and report the OpenQ4 renderer validation matrix.

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


SAFE_TIERS = ("auto", "legacy", "gl33", "gl41", "gl43", "gl45", "gl46")

SELFTEST_CHECKS = [
    ["RendererContextLadder self-test passed"],
    ["RendererTierSelect self-test passed"],
    ["RendererUpload self-test passed"],
    ["RendererGpuTimer self-test passed", "RendererGpuTimer self-test skipped"],
    ["RendererScenePacket self-test passed"],
    ["RendererRenderGraph self-test passed"],
    ["RendererRenderGraphResource self-test passed", "RendererRenderGraphResource self-test skipped"],
    ["RendererMaterialResourceTable self-test passed", "RendererMaterialResourceTable self-test skipped"],
    ["RendererGeometryResource self-test passed"],
    ["RendererGLStateCache self-test passed", "RendererGLStateCache self-test skipped"],
    ["RendererModernGLShaderLibrary self-test passed"],
    ["RendererModernGLDrawPlan self-test passed"],
    ["RendererModernGLSubmitPlan self-test passed"],
    ["RendererModernGLExecutor self-test passed"],
]

STARTUP_CHECKS = [
    ["created OpenGL context"],
    ["Selected renderer tier:"],
    ["GL context profile:"],
    ["GL context request:"],
    ["Renderer caps:"],
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
        "id": "mp-q4dm1-listen",
        "mode": "MP",
        "map": "mp/q4dm1",
        "purpose": "listen-server and local-client MP renderer parity",
    },
]


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
    preferred = install_dir / f"OpenQ4-client_{host_arch()}{suffix}"
    if preferred.exists():
        return preferred

    candidates = sorted(install_dir.glob("OpenQ4-client_*"))
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    for candidate in candidates:
        if candidate.is_file():
            return candidate

    raise FileNotFoundError(f"OpenQ4 client executable not found under {install_dir}")


def default_basepath() -> str:
    if os.name == "nt":
        return r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"
    return ""


def sanitize_case_id(case_id: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", case_id)


def common_args(root: Path, case_id: str, basepath: str, savepath: Path) -> list[str]:
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
        "fs_savepath",
        str(savepath),
        "+set",
        "fs_devpath",
        str(root / ".install"),
        "+set",
        "fs_game",
        "baseoq4",
    ]
    if basepath:
        args += ["+set", "fs_basepath", basepath]
    return args


def build_safe_cases(tiers: tuple[str, ...]) -> list[dict[str, Any]]:
    selftest_commands = [
        "+set",
        "r_rendererMetrics",
        "2",
        "+set",
        "r_rendererModernExecutor",
        "1",
        "+set",
        "r_rendererModernSubmit",
        "0",
        "+rendererContextLadderSelfTest",
        "+rendererTierSelfTest",
        "+rendererUploadSelfTest",
        "+rendererGpuTimerSelfTest",
        "+rendererScenePacketSelfTest",
        "+rendererRenderGraphSelfTest",
        "+rendererRenderGraphResourceSelfTest",
        "+rendererMaterialResourceTableSelfTest",
        "+rendererGeometryResourceSelfTest",
        "+rendererGLStateCacheSelfTest",
        "+rendererShaderLibrarySelfTest",
        "+rendererModernGLDrawPlanSelfTest",
        "+rendererModernGLSubmitPlanSelfTest",
        "+rendererModernGLExecutorSelfTest",
        "+gfxInfo",
    ]

    cases: list[dict[str, Any]] = [
        {
            "id": "renderer-foundation-selftests",
            "category": "selftest",
            "description": "Renderer foundation, upload, metrics, packet, graph, material, geometry, shader, draw, submit, and executor self-tests.",
            "args": selftest_commands,
            "checks": SELFTEST_CHECKS + [["Selected renderer tier:"], ["GL context request:"]],
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
                ["RendererClusterGrid self-test passed"],
                ["lights=6"],
                ["overflow="],
                ["ubo=1"],
                ["overlay=1"],
                ["Modern clustered lighting:"],
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
                ["pixels="],
                ["reads="],
                ["overlay=1"],
                ["Modern GL executor:"],
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
                ["draws="],
                ["opaque="],
                ["transparent="],
                ["reads="],
                ["Modern forward+:"],
                ["modernForwardPlus req=1"],
                ["rendererMetrics forwardPlus(req=1"],
                ["Modern clustered lighting:"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
    ]

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
            "id": "present-vsync1-fps30",
            "category": "presentation-startup",
            "description": "Low-fps capped presentation startup probe.",
            "args": [
                "+set",
                "r_swapInterval",
                "1",
                "+set",
                "com_maxfps",
                "30",
                "+gfxInfo",
            ],
            "checks": STARTUP_CHECKS,
        },
    ]

    return cases


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


def evaluate_checks(text: str, checks: list[list[str]]) -> tuple[bool, list[str]]:
    missing: list[str] = []
    for alternatives in checks:
        if not any(pattern in text for pattern in alternatives):
            missing.append(" or ".join(alternatives))
    failed_markers = [
        "self-test failed",
        "Fatal Error",
        "could not initialize OpenGL",
        "Unable to initialize OpenGL",
    ]
    for marker in failed_markers:
        if marker in text:
            missing.append(f"unexpected marker: {marker}")
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


def run_case(
    root: Path,
    executable: Path,
    output_dir: Path,
    savepath: Path,
    basepath: str,
    case: dict[str, Any],
    timeout_seconds: int,
) -> dict[str, Any]:
    case_id = case["id"]
    log_name = f"openq4_validation_{sanitize_case_id(case_id)}.log"
    stdout_path = output_dir / f"{sanitize_case_id(case_id)}.out.txt"
    stderr_path = output_dir / f"{sanitize_case_id(case_id)}.err.txt"
    log_path_guess = find_log(savepath, log_name)
    if log_path_guess is not None:
        log_path_guess.unlink()

    args = common_args(root, case_id, basepath, savepath) + case["args"] + ["+quit"]
    started = time.time()
    timed_out = False
    with stdout_path.open("w", encoding="utf-8", errors="replace") as stdout_file, stderr_path.open("w", encoding="utf-8", errors="replace") as stderr_file:
        process = subprocess.Popen(
            [str(executable)] + args,
            cwd=str(root / ".install"),
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
    log_path = find_log(savepath, log_name)
    log_text = ""
    if log_path is not None:
        log_text = log_path.read_text(encoding="utf-8", errors="replace")
    else:
        if stdout_path.exists():
            log_text += stdout_path.read_text(encoding="utf-8", errors="replace")
        if stderr_path.exists():
            log_text += "\n" + stderr_path.read_text(encoding="utf-8", errors="replace")

    checks_ok, missing = evaluate_checks(log_text, case["checks"])
    ok = exit_code == 0 and not timed_out and log_path is not None and checks_ok
    return {
        "id": case_id,
        "category": case["category"],
        "description": case["description"],
        "status": "pass" if ok else "fail",
        "exitCode": exit_code,
        "timedOut": timed_out,
        "elapsedSeconds": round(elapsed, 2),
        "log": str(log_path) if log_path is not None else "",
        "stdout": str(stdout_path),
        "stderr": str(stderr_path),
        "missing": missing,
        "summary": extract_summary(log_text),
    }


def write_reports(output_dir: Path, results: list[dict[str, Any]], metadata: dict[str, Any]) -> tuple[Path, Path]:
    report_json = output_dir / "renderer_validation_report.json"
    report_md = output_dir / "renderer_validation_report.md"

    payload = {
        "metadata": metadata,
        "results": results,
        "manualGameplayMatrix": MANUAL_GAMEPLAY_MATRIX,
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
        "| Status | Case | Category | Context | Selected Tier | Log |",
        "|---|---|---|---|---|---|",
    ]
    for result in results:
        summary = result["summary"]
        context = summary.get("context", summary.get("contextProfile", ""))
        selected = summary.get("selectedTier", "")
        log = result["log"] or result["stdout"]
        lines.append(
            f"| {result['status']} | `{result['id']}` | {result['category']} | {context} | {selected} | `{log}` |"
        )
        if result["missing"]:
            lines.append(f"|  | missing |  | {'; '.join(result['missing'])} |  |  |")

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

    report_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return report_json, report_md


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tiers", default=",".join(SAFE_TIERS), help="Comma-separated r_glTier startup probes.")
    parser.add_argument("--timeout", type=int, default=60, help="Per-case timeout in seconds.")
    parser.add_argument("--basepath", default=default_basepath(), help="Quake 4 install/base path. Omit or set empty to skip fs_basepath.")
    parser.add_argument("--savepath", default="", help="Save path root. Defaults to <repo>/.home.")
    parser.add_argument("--output-dir", default="", help="Report/output directory. Defaults to <repo>/.tmp/renderer-validation/<timestamp>.")
    parser.add_argument("--list", action="store_true", help="List automated and manual cases without running them.")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    root = repo_root()
    tiers = tuple(tier.strip() for tier in args.tiers.split(",") if tier.strip())
    safe_cases = build_safe_cases(tiers)

    if args.list:
        print("Automated safe cases:")
        for case in safe_cases:
            print(f"  {case['id']}: {case['description']}")
        print("\nManual gameplay cases:")
        for case in MANUAL_GAMEPLAY_MATRIX:
            print(f"  {case['id']}: {case['mode']} {case['map']} - {case['purpose']}")
        return 0

    executable = find_client_executable(root)
    savepath = Path(args.savepath).resolve() if args.savepath else root / ".home"
    savepath.mkdir(parents=True, exist_ok=True)
    timestamp = time.strftime("%Y%m%d-%H%M%S")
    output_dir = Path(args.output_dir).resolve() if args.output_dir else root / ".tmp" / "renderer-validation" / timestamp
    output_dir.mkdir(parents=True, exist_ok=True)

    basepath = args.basepath
    if basepath and not Path(basepath).exists():
        print(f"warning: basepath does not exist, omitting fs_basepath: {basepath}", file=sys.stderr)
        basepath = ""

    results = []
    for case in safe_cases:
        print(f"running {case['id']}...")
        result = run_case(root, executable, output_dir, savepath, basepath, case, args.timeout)
        print(f"  {result['status']} ({result['elapsedSeconds']}s)")
        results.append(result)

    metadata = {
        "generated": time.strftime("%Y-%m-%d %H:%M:%S %z"),
        "host": f"{platform.system()} {platform.release()} {platform.machine()}",
        "executable": str(executable),
        "savepath": str(savepath),
        "basepath": basepath,
    }
    report_json, report_md = write_reports(output_dir, results, metadata)
    print(f"wrote {report_md}")
    print(f"wrote {report_json}")

    return 0 if all(result["status"] == "pass" for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
