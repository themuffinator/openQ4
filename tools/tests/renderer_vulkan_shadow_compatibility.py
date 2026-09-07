#!/usr/bin/env python3
"""Regression contracts for Vulkan shadow ownership and graceful fallback."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import math
import re
import sys
import tempfile
from pathlib import Path
from types import ModuleType


ROOT = Path(__file__).resolve().parents[2]
TEST_PATH = "tools/tests/renderer_vulkan_shadow_compatibility.py"


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def compact(value: str) -> str:
    """Ignore indentation and line wrapping while retaining source semantics."""
    return " ".join(value.split())


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_compact(haystack: str, needle: str, context: str) -> None:
    if compact(needle) not in compact(haystack):
        raise AssertionError(f"Missing compact source contract {compact(needle)!r} in {context}")


def require_order(haystack: str, needles: tuple[str, ...], context: str) -> None:
    compact_haystack = compact(haystack)
    previous = -1
    for needle in needles:
        compact_needle = compact(needle)
        position = compact_haystack.find(compact_needle, previous + 1)
        if position == -1:
            raise AssertionError(f"Missing {compact_needle!r} in {context}")
        if position <= previous:
            raise AssertionError(f"Expected ordered source contracts in {context}: {needles!r}")
        previous = position


def braced_body(source: str, marker: str, context: str) -> str:
    search_from = 0
    while True:
        start = source.find(marker, search_from)
        if start == -1:
            raise AssertionError(f"Missing function definition {marker!r} in {context}")

        suffix_start = start + len(marker)
        marker_brace = marker.rfind("{")
        if marker_brace != -1:
            opening_brace = start + marker_brace
            break
        opening_brace = source.find("{", suffix_start)
        semicolon = source.find(";", suffix_start)
        if opening_brace != -1 and (semicolon == -1 or opening_brace < semicolon):
            break

        # Skip forward declarations and continue at the next occurrence of the
        # marker. Shadow-cache storage validators intentionally need prototypes
        # because cache expiry calls them before their definitions.
        search_from = suffix_start

    depth = 0
    for index in range(opening_brace, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find closing brace for {marker!r} in {context}")


def load_test_module(relative_path: str, module_name: str) -> ModuleType:
    path = ROOT / relative_path
    spec = importlib.util.spec_from_file_location(module_name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"Could not load Python test module {relative_path}")
    module = importlib.util.module_from_spec(spec)
    # dataclasses and other runtime type helpers resolve the defining module
    # through sys.modules while its top-level declarations execute.
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


def write_rgb_tga(path: Path, width: int, height: int, value: int) -> None:
    header = bytearray(18)
    header[2] = 2
    header[12:14] = width.to_bytes(2, "little")
    header[14:16] = height.to_bytes(2, "little")
    header[16] = 24
    pixel = bytes((value, value, value))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(header) + pixel * (width * height))


def validate_shadow_effectiveness_image_gate() -> None:
    gameplay = load_test_module(
        "tools/tests/renderer_gameplay_benchmark.py",
        "openq4_renderer_gameplay_shadow_image_contract",
    )
    tmp_root = ROOT / ".tmp"
    tmp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="shadow-image-contract-", dir=tmp_root
    ) as temp_name:
        temp = Path(temp_name)
        savepath = temp / "save"
        actual = savepath / "baseoq4" / "screenshots" / "renderer-bench" / "sp_0.tga"
        references = temp / "references"
        reference = references / actual.name
        write_rgb_tga(actual, 4, 4, 0)
        write_rgb_tga(reference, 4, 4, 0)

        identical = gameplay.compare_screenshot_difference_if_requested(
            actual, savepath, references, 0.1, 1, "shadow-contract"
        )
        if identical.get("status") != "difference-compared" or identical.get("pass"):
            raise AssertionError(
                "Identical shadows-on/off TGAs must not satisfy the shadow effectiveness gate"
            )

        write_rgb_tga(reference, 4, 4, 255)
        parity = gameplay.compare_screenshot_if_requested(
            actual, savepath, references, 0.0, 0, True, "shadow-contract"
        )
        if parity.get("status") != "compared" or parity.get("pass"):
            raise AssertionError(
                "A non-identical shared/classic TGA must not satisfy exact parity thresholds"
            )

        different = gameplay.compare_screenshot_difference_if_requested(
            actual, savepath, references, 1.0, 48, "shadow-contract"
        )
        if (
            different.get("status") != "difference-compared"
            or not different.get("pass")
            or different.get("differingChannels") != 48
        ):
            raise AssertionError(
                "A material engine-TGA shadow delta must satisfy the effectiveness gate"
            )

        generated_references = temp / "generated" / "savepaths"
        generated_reference = (
            generated_references
            / "shadow-contract"
            / "baseoq4"
            / "screenshots"
            / "renderer-bench"
            / "sp_0.tga"
        )
        write_rgb_tga(generated_reference, 4, 4, 0)
        generated_tree = gameplay.compare_screenshot_if_requested(
            actual,
            savepath,
            generated_references,
            0.0,
            0,
            True,
            "shadow-contract",
        )
        if generated_tree.get("status") != "compared" or not generated_tree.get("pass"):
            raise AssertionError(
                "A reference tree generated by the gameplay runner must be consumable directly"
            )

        missing = gameplay.compare_screenshot_difference_if_requested(
            actual, savepath, temp / "missing", 0.1, 1, "shadow-contract"
        )
        if missing.get("status") != "missing-difference-reference" or missing.get("pass"):
            raise AssertionError("A missing shadows-off TGA reference must fail closed")


def validate_shared_interaction_shadow_runtime_contract() -> None:
    gameplay = load_test_module(
        "tools/tests/renderer_gameplay_benchmark.py",
        "openq4_renderer_gameplay_shadow_runtime_contract",
    )
    aggregate = (
        "Renderer shared interaction: requested=1 prepared=1 valid=1 views=1 "
        "ready=1 fallback=0 lights=1 surfaces=1 primitives=2 draw=2 noop=0 "
        "shadow=1/2/2+0 volumes=1+0 maps=2+1 modes=2+0 csm=2 "
        "stages=1/0+1/0 receivers=1/1/0 hash=0123456789abcdef status=ready "
        "GL=1/0/0 VK=0/0/0"
    )
    view = (
        "Renderer shared interaction view[0]: scene=0 pass=1 shadowPass=2/3 "
        "mode=hybrid ready=1 failure=none detail=0 drawPacket=-1 light=-1 "
        "receiver=-1 stage=-1 lights=1 surfaces=1 primitives=2/2 noop=0 "
        "shadow=1/2/2+0 volumes=1+0 maps=2+1 modes=2+0 csm=2 "
        "hash=0123456789abcdef "
        "GL=1/none/0/2+0/2+0+1+0/2+1 "
        "VK=0/none/0/0+0/0+0+0+0/0+0"
    )
    maps = (
        "Renderer shared interaction map view[0] light=0 receiver=local "
        "mode=hybrid class=projected cascades=3 alias=0 "
        "plan=0000000100000001 generation=1 casters=1+1 "
        "features=1+0+0+0 hash=1111111111111111\n"
        "Renderer shared interaction map view[0] light=0 receiver=global "
        "mode=projected class=projected cascades=3 alias=1 "
        "plan=0000000100000002 generation=1 casters=1+0 "
        "features=1+0+0+0 hash=2222222222222222"
    )
    summary = gameplay.extract_summary("\n".join((aggregate, view, maps)))
    spec = gameplay.RunSpec(
        case_id="synthetic-hybrid",
        mode="SP",
        map_name="maps/tools/mv2",
        budget_map_name="maps/tools/mv2",
        purpose="synthetic mapped receiver accounting",
        path_name="spawn-static",
        tier="auto",
        maxfps="240",
        swap_interval="0",
        display_mode="windowed",
        shadow_preset="mapped",
        renderer="best",
        render_api="gl",
        interaction_expectation="owned",
        interaction_shadow_expectation="hybrid",
    )
    failures = gameplay.evaluate_shared_interaction_evidence(spec, summary)
    if failures:
        raise AssertionError(
            "Complete hybrid map/stencil runtime evidence must pass: "
            f"{failures!r}"
        )

    bad_backend = dict(summary)
    bad_backend["sharedInteractionView"] = view.replace(
        "/2+1 VK=", "/0+0 VK="
    )
    failures = gameplay.evaluate_shared_interaction_evidence(spec, bad_backend)
    if not any("execution=" in failure for failure in failures):
        raise AssertionError(
            "Mapped backend pass-count mismatch must fail exact execution reconciliation"
        )

    malformed_shadow = dict(summary)
    malformed_shadow["sharedInteraction"] = aggregate.replace(
        "shadow=1/2/2+0 volumes=1+0", "shadow=malformed volumes=malformed"
    )
    failures = gameplay.evaluate_shared_interaction_evidence(spec, malformed_shadow)
    if not any("shadow/volume counters=missing" in failure for failure in failures):
        raise AssertionError(
            "Malformed shadow counters must fail closed without raising during backend reconciliation"
        )

    duplicate_map = dict(summary)
    duplicate_map["sharedInteractionMaps"] = maps + "\n" + maps.splitlines()[0]
    failures = gameplay.evaluate_shared_interaction_evidence(spec, duplicate_map)
    if not any("per-map reconciliation=" in failure for failure in failures):
        raise AssertionError(
            "Duplicate per-map evidence must fail exact pass identity reconciliation"
        )

    dynamic_spec = gameplay.RunSpec(
        case_id="synthetic-dynamic",
        mode="SP",
        map_name="game/airdefense2",
        budget_map_name="game/airdefense2",
        purpose="synthetic dynamic mapped-caster evidence",
        path_name="spawn-static",
        tier="auto",
        maxfps="240",
        swap_interval="0",
        display_mode="windowed",
        shadow_preset="csm",
        renderer="best",
        render_api="gl",
        interaction_expectation="owned",
        interaction_shadow_expectation="dynamic",
    )
    failures = gameplay.evaluate_shared_interaction_evidence(dynamic_spec, summary)
    if not any("dynamic caster feature missing=" in failure for failure in failures):
        raise AssertionError(
            "A dynamic target must fail unless a sealed map pass reports dynamic=1"
        )

    perforated_spec = gameplay.RunSpec(
        case_id="synthetic-perforated",
        mode="SP",
        map_name="game/storage2",
        budget_map_name="game/storage2",
        purpose="synthetic perforated mapped-caster evidence",
        path_name="spawn-static",
        tier="auto",
        maxfps="240",
        swap_interval="0",
        display_mode="windowed",
        shadow_preset="csm",
        renderer="best",
        render_api="gl",
        interaction_expectation="owned",
        interaction_shadow_expectation="perforated",
    )
    failures = gameplay.evaluate_shared_interaction_evidence(perforated_spec, summary)
    if not any("perforated caster feature missing=" in failure for failure in failures):
        raise AssertionError(
            "A perforated target must fail unless a sealed map pass reports alpha=1"
        )

    feature_summary = dict(summary)
    feature_summary["sharedInteractionMaps"] = maps.replace(
        "features=1+0+0+0", "features=1+1+1+0"
    )
    for feature_spec in (dynamic_spec, perforated_spec):
        failures = gameplay.evaluate_shared_interaction_evidence(
            feature_spec, feature_summary
        )
        if failures:
            raise AssertionError(
                "Explicit dynamic/alpha per-map evidence must satisfy its stock "
                f"target: {feature_spec.interaction_shadow_expectation} {failures!r}"
            )

    zero_shadow_summary = dict(summary)
    zero_shadow_summary["sharedInteraction"] = aggregate.replace(
        "shadow=1/2/2+0", "shadow=0/0/0+0"
    )
    zero_shadow_summary["sharedInteractionView"] = (
        view.replace("shadow=1/2/2+0", "shadow=0/0/0+0")
        .replace("/2+0+1+0/2+1 VK=", "/0+0+1+0/2+1 VK=")
    )
    failures = gameplay.evaluate_shared_interaction_evidence(
        spec, zero_shadow_summary
    )
    if not any("mapped caster accounting=" in failure for failure in failures):
        raise AssertionError(
            "Per-map caster records must not reconcile against zero aggregate shadow casters"
        )

    dual_backend_summary = dict(summary)
    dual_backend_summary["sharedInteraction"] = aggregate.replace(
        "VK=0/0/0", "VK=1/0/0"
    )
    dual_backend_summary["sharedInteractionView"] = view.replace(
        "VK=0/none/0/0+0/0+0+0+0/0+0",
        "VK=1/none/0/2+0/2+0+1+0/2+1",
    )
    failures = gameplay.evaluate_shared_interaction_evidence(
        spec, dual_backend_summary
    )
    if not any("inactive backend" in failure for failure in failures):
        raise AssertionError(
            "A selected GL run must reject simultaneous Vulkan ownership and execution"
        )

    mixed_aggregate = (
        "Renderer shared interaction: requested=1 prepared=1 valid=1 views=1 "
        "ready=1 fallback=0 lights=1 surfaces=1 primitives=1 draw=1 noop=0 "
        "shadow=1/1/1+0 volumes=1+0 maps=1+0 modes=1+0 csm=0 "
        "stages=1/0+1/0 receivers=1/0/0 hash=0123456789abcdef status=ready "
        "GL=1/0/0 VK=0/0/0"
    )
    mixed_view = (
        "Renderer shared interaction view[0]: scene=0 pass=1 shadowPass=2/3 "
        "mode=hybrid ready=1 failure=none detail=0 drawPacket=-1 light=-1 "
        "receiver=-1 stage=-1 lights=1 surfaces=1 primitives=1/1 noop=0 "
        "shadow=1/1/1+0 volumes=1+0 maps=1+0 modes=1+0 csm=0 "
        "hash=0123456789abcdef "
        "GL=1/none/0/1+0/1+0+1+0/1+0 "
        "VK=0/none/0/0+0/0+0+0+0/0+0"
    )
    mixed_map = (
        "Renderer shared interaction map view[0] light=0 receiver=local "
        "mode=projected class=projected cascades=1 alias=0 "
        "plan=0000000100000001 generation=1 casters=1+0 "
        "features=1+0+0+0 hash=1111111111111111"
    )
    mixed_spec = gameplay.RunSpec(
        case_id="synthetic-mixed",
        mode="SP",
        map_name="maps/tools/mv2",
        budget_map_name="maps/tools/mv2",
        purpose="synthetic mixed-light identity",
        path_name="spawn-static",
        tier="auto",
        maxfps="240",
        swap_interval="0",
        display_mode="windowed",
        shadow_preset="mixed",
        renderer="best",
        render_api="gl",
        interaction_expectation="owned",
        interaction_shadow_expectation="mixed",
    )
    failures = gameplay.evaluate_shared_interaction_evidence(
        mixed_spec,
        gameplay.extract_summary("\n".join((mixed_aggregate, mixed_view, mixed_map))),
    )
    if not any("map/stencil mix=" in failure for failure in failures):
        raise AssertionError(
            "Mixed ownership must include a stencil shadow light outside the mapped-light identities"
        )

    fallback_aggregate = (
        "Renderer shared interaction: requested=1 prepared=1 valid=0 views=1 "
        "ready=0 fallback=1 lights=0 surfaces=0 primitives=0 draw=0 noop=0 "
        "shadow=0/0/0+0 volumes=0+0 maps=0+0 modes=0+0 csm=0 "
        "stages=0/0+0/0 receivers=0/0/0 hash=0000000000000000 status=fallback "
        "GL=0/1/0 VK=0/0/0"
    )
    translucent_view = (
        "Renderer shared interaction view[0]: scene=0 pass=1 shadowPass=2/3 "
        "mode=none ready=0 failure=shadowMap detail=6 drawPacket=-1 light=0 "
        "receiver=-1 stage=-1 lights=0 surfaces=0 primitives=0/0 noop=0 "
        "shadow=0/0/0+0 volumes=0+0 maps=0+0 modes=0+0 csm=0 "
        "hash=0000000000000000 "
        "GL=2/shadowMap/6/0+0/0+0+0+0/0+0 "
        "VK=0/none/0/0+0/0+0+0+0/0+0"
    )
    translucent_spec = gameplay.RunSpec(
        case_id="synthetic-translucent",
        mode="SP",
        map_name="game/medlabs",
        budget_map_name="game/medlabs",
        purpose="synthetic translucent-moment fallback",
        path_name="spawn-static",
        tier="auto",
        maxfps="240",
        swap_interval="0",
        display_mode="windowed",
        shadow_preset="translucent",
        renderer="best",
        render_api="gl",
        interaction_expectation="fallback",
        interaction_shadow_expectation="translucent-fallback",
    )
    translucent_summary = gameplay.extract_summary(
        "\n".join((fallback_aggregate, translucent_view))
    )
    failures = gameplay.evaluate_shared_interaction_evidence(
        translucent_spec, translucent_summary
    )
    if failures:
        raise AssertionError(
            f"Exact translucent shadow-chain fallback must pass: {failures!r}"
        )
    wrong_fallback_summary = dict(translucent_summary)
    wrong_fallback_summary["sharedInteractionView"] = (
        translucent_view.replace("failure=shadowMap detail=6", "failure=deform detail=13")
        .replace("GL=2/shadowMap/6/", "GL=2/deform/13/")
    )
    failures = gameplay.evaluate_shared_interaction_evidence(
        translucent_spec, wrong_fallback_summary
    )
    if not any(
        "translucent-moment fallback=" in failure for failure in failures
    ):
        raise AssertionError(
            "An unrelated named fallback must not satisfy translucent-moment acceptance"
        )

    budget_aggregate = (
        aggregate.replace(
            "lights=1 surfaces=1 primitives=2 draw=2",
            "lights=4 surfaces=4 primitives=4 draw=4",
        )
        .replace(
            "shadow=1/2/2+0 volumes=1+0 maps=2+1 modes=2+0 csm=2",
            "shadow=4/4/4+0 volumes=0+0 maps=4+0 modes=1+3 csm=0",
        )
        .replace("GL=1/0/0 VK=0/0/0", "GL=0/1/0 VK=0/0/0")
    )
    budget_view = (
        view.replace(
            "lights=1 surfaces=1 primitives=2/2",
            "lights=4 surfaces=4 primitives=4/4",
        )
        .replace(
            "shadow=1/2/2+0 volumes=1+0 maps=2+1 modes=2+0 csm=2",
            "shadow=4/4/4+0 volumes=0+0 maps=4+0 modes=1+3 csm=0",
        )
        .replace(
            "GL=1/none/0/2+0/2+0+1+0/2+1",
            "GL=2/backendRejected/-1892967296/0+0/0+0+0+0/0+0",
        )
    )
    budget_maps = (
        "Renderer shared interaction map view[0] light=0 receiver=local "
        "mode=projected class=projected cascades=1 alias=0 "
        "plan=0000000100000001 generation=1 casters=1+0 "
        "features=1+0+0+0 hash=1111111111111111\n"
        "Renderer shared interaction map view[0] light=1 receiver=local "
        "mode=point class=point cascades=6 alias=0 "
        "plan=0000000100000002 generation=1 casters=1+0 "
        "features=1+0+0+0 hash=2222222222222222\n"
        "Renderer shared interaction map view[0] light=2 receiver=local "
        "mode=point class=point cascades=6 alias=0 "
        "plan=0000000100000003 generation=1 casters=1+0 "
        "features=1+0+0+0 hash=3333333333333333\n"
        "Renderer shared interaction map view[0] light=3 receiver=local "
        "mode=point class=point cascades=6 alias=0 "
        "plan=0000000100000004 generation=1 casters=1+0 "
        "features=1+0+0+0 hash=4444444444444444"
    )
    budget_spec = gameplay.RunSpec(
        case_id="synthetic-map-budget",
        mode="SP",
        map_name="maps/tools/mv2",
        budget_map_name="maps/tools/mv2",
        purpose="synthetic map-admission fallback",
        path_name="spawn-static",
        tier="auto",
        maxfps="240",
        swap_interval="0",
        display_mode="windowed",
        shadow_preset="map-budget-fallback",
        renderer="best",
        render_api="gl",
        interaction_expectation="fallback",
        interaction_shadow_expectation="map-budget-fallback",
    )
    budget_summary = gameplay.extract_summary(
        "\n".join((budget_aggregate, budget_view, budget_maps))
    )
    failures = gameplay.evaluate_shared_interaction_evidence(
        budget_spec, budget_summary
    )
    if failures:
        raise AssertionError(
            f"Exact map-transaction admission fallback must pass: {failures!r}"
        )
    wrong_budget_summary = dict(budget_summary)
    wrong_budget_summary["sharedInteractionView"] = budget_view.replace(
        "-1892967296", "1801000001"
    )
    failures = gameplay.evaluate_shared_interaction_evidence(
        budget_spec, wrong_budget_summary
    )
    if not any(
        "map-budget admission fallback=" in failure for failure in failures
    ):
        raise AssertionError(
            "An unrelated shadow-cache rejection must not satisfy map-admission fallback acceptance"
        )

    wrong_budget_light_summary = dict(budget_summary)
    wrong_budget_light_summary["sharedInteractionView"] = budget_view.replace(
        "-1892967296", "-1893967296"
    )
    failures = gameplay.evaluate_shared_interaction_evidence(
        budget_spec, wrong_budget_light_summary
    )
    if not any(
        "map-budget admission fallback=" in failure for failure in failures
    ):
        raise AssertionError(
            "A map-transaction rejection on the wrong controlled light must not satisfy the exact budget target"
        )

    duplicate_budget_maps = dict(budget_summary)
    budget_map_lines = budget_maps.splitlines()
    duplicate_budget_maps["sharedInteractionMaps"] = "\n".join(
        (*budget_map_lines[:-1], budget_map_lines[0])
    )
    failures = gameplay.evaluate_shared_interaction_evidence(
        budget_spec, duplicate_budget_maps
    )
    if not any("fallback mapped plan=" in failure for failure in failures):
        raise AssertionError(
            "Duplicate map identities must not satisfy a prepared map-budget fallback plan"
        )

    zero_budget_shadow = dict(budget_summary)
    zero_budget_shadow["sharedInteraction"] = budget_aggregate.replace(
        "shadow=4/4/4+0", "shadow=0/0/0+0"
    )
    failures = gameplay.evaluate_shared_interaction_evidence(
        budget_spec, zero_budget_shadow
    )
    if not any(
        "fallback mapped caster accounting=" in failure for failure in failures
    ):
        raise AssertionError(
            "Prepared budget maps must not reconcile against zero shadow-light/caster accounting"
        )

    undercounted_budget_shadow = dict(budget_summary)
    undercounted_budget_shadow["sharedInteraction"] = budget_aggregate.replace(
        "shadow=4/4/4+0", "shadow=4/1/1+0"
    )
    failures = gameplay.evaluate_shared_interaction_evidence(
        budget_spec, undercounted_budget_shadow
    )
    if not any(
        "fallback mapped caster accounting=" in failure for failure in failures
    ):
        raise AssertionError(
            "Per-light mapped caster minima must not exceed aggregate caster accounting"
        )


def validate_shared_interaction_shadow_profile_contract() -> None:
    gameplay = load_test_module(
        "tools/tests/renderer_gameplay_benchmark.py",
        "openq4_renderer_gameplay_shadow_profile_contract",
    )

    interaction_args = gameplay.parse_args(
        ["--profile", "interaction", "--pacing-only", "--dry-run"]
    )
    interaction_specs = gameplay.build_specs(interaction_args)
    interaction_contract = [
        (
            spec.case_id,
            spec.shadow_preset,
            spec.interaction_expectation,
            spec.interaction_shadow_expectation,
        )
        for spec in interaction_specs
    ]
    expected_interaction_contract = [
        ("sp-mv2-interaction", "unshadowed", "owned", "none"),
        ("sp-mv2-interaction", "stencil", "owned", "stencil"),
        ("sp-mv2-interaction", "mapped", "owned", "mapped"),
        ("sp-mv2-interaction", "mixed", "owned", "mixed"),
        (
            "sp-mv2-interaction",
            "map-budget-fallback",
            "fallback",
            "map-budget-fallback",
        ),
    ]
    if interaction_contract != expected_interaction_contract:
        raise AssertionError(
            "Controlled interaction profile must cover unshadowed, stencil, "
            "mapped, mixed, and atomic map-admission fallback exactly: "
            f"{interaction_contract!r}"
        )

    stock_args = gameplay.parse_args(
        ["--profile", "interaction-shadow-stock", "--pacing-only", "--dry-run"]
    )
    if ("g_stopTime", "1") not in stock_args.launch_cvars:
        raise AssertionError(
            "Stock interaction-shadow startup must begin from deterministic tic zero"
        )
    if ("ui_showGun", "0") not in stock_args.launch_cvars:
        raise AssertionError(
            "Stock interaction-shadow captures must exclude unsupported viewmodel interactions"
        )
    if ("r_multiSamples", "0") not in stock_args.launch_cvars:
        raise AssertionError(
            "Stock interaction-shadow captures must disable multisampled game targets"
        )
    direct_target_cvars = {
        "g_renderFastNoPost": "1",
        "g_renderFastNoPostDirect": "1",
        "g_renderCasUpscale": "0",
        "r_postAA": "0",
        "r_screenFraction": "100",
        "r_bloom": "0",
        "r_motionBlur": "0",
        "r_ssao": "0",
        "r_hdrToneMap": "0",
        "r_hdrDebugView": "0",
    }
    stock_extra_cvars = dict(stock_args.extra_cvars)
    if any(
        stock_extra_cvars.get(name) != value
        for name, value in direct_target_cvars.items()
    ):
        raise AssertionError(
            "Stock interaction-shadow captures must make the direct root target deterministic"
        )
    if ("g_stopTime", "0") not in stock_args.extra_cvars:
        raise AssertionError(
            "Stock interaction-shadow scenes must advance during their settle interval"
        )
    if "g_stopTime 1" not in stock_args.exec_commands:
        raise AssertionError(
            "Stock interaction-shadow scenes must freeze again before sampling"
        )
    stock_specs = gameplay.build_specs(stock_args)
    stock_contract = [
        (
            spec.case_id,
            spec.shadow_preset,
            spec.interaction_expectation,
            spec.interaction_shadow_expectation,
        )
        for spec in stock_specs
    ]
    expected_stock_contract = [
        ("shadow-projected-airdefense2", "unshadowed", "owned", "none"),
        ("shadow-projected-airdefense2", "mapped", "owned", "projected"),
        ("shadow-point-airdefense2", "unshadowed", "owned", "none"),
        ("shadow-point-airdefense2", "mapped", "owned", "point"),
        ("shadow-csm-airdefense1", "unshadowed", "owned", "none"),
        ("shadow-csm-airdefense1", "csm", "owned", "csm"),
        ("shadow-character-airdefense2", "unshadowed", "owned", "none"),
        ("shadow-character-airdefense2", "csm", "owned", "dynamic"),
        ("shadow-cutout-storage2", "unshadowed", "owned", "none"),
        ("shadow-cutout-storage2", "csm", "owned", "perforated"),
        ("shadow-hybrid-storage2", "unshadowed", "owned", "none"),
        ("shadow-hybrid-storage2", "mapped", "owned", "hybrid"),
        (
            "shadow-translucent-medlabs",
            "translucent",
            "fallback",
            "translucent-fallback",
        ),
    ]
    if stock_contract != expected_stock_contract:
        raise AssertionError(
            "Stock interaction-shadow profile must cover projected, point, CSM, "
            "dynamic, perforated, same-light hybrid, and translucent-moment "
            f"fallback exactly: {stock_contract!r}"
        )

    for spec in (*interaction_specs, *stock_specs):
        unshadowed_id = spec.id_for_shadow_preset("unshadowed")
        if spec.shadow_preset == "unshadowed" and unshadowed_id != spec.id:
            raise AssertionError(
                "Unshadowed runs must use their own capture id as the image-difference reference key"
            )
        if spec.shadow_preset != "unshadowed" and unshadowed_id == spec.id:
            raise AssertionError(
                "Shadowed runs must resolve image-difference evidence through the matching unshadowed capture id"
            )

    with contextlib.redirect_stderr(io.StringIO()):
        try:
            gameplay.parse_args(
                [
                    "--profile",
                    "interaction",
                    "--pacing-only",
                    "--require-references",
                ]
            )
        except SystemExit as exc:
            if exc.code != 2:
                raise
        else:
            raise AssertionError(
                "--require-references without --reference-dir must fail during argument validation"
            )

    shadows_off_args = gameplay.parse_args(
        [
            "--profile",
            "interaction",
            "--pacing-only",
            "--shadow-presets",
            "map-budget-fallback",
            "--set-cvar",
            "r_shadows=0",
            "--set-cvar",
            "r_useShadowMap=0",
        ]
    )
    shadows_off_specs = gameplay.build_specs(shadows_off_args)
    if any(
        spec.interaction_expectation != "owned"
        or spec.interaction_shadow_expectation != "none"
        for spec in shadows_off_specs
    ):
        raise AssertionError(
            "Shadows-off reference generation must not retain a shadow fallback expectation"
        )

    matrix = load_test_module(
        "tools/tests/renderer_validation_matrix.py",
        "openq4_renderer_validation_shadow_profile_contract",
    )
    expected_matrix_keys = {
        (profile, backend)
        for profile in ("interaction", "interaction-shadow-stock")
        for backend in ("gl", "vk")
    }

    def interaction_matrix_failures(entries: list[dict[str, object]]) -> list[str]:
        indexed: dict[tuple[object, object], dict[str, object]] = {}
        failures: list[str] = []
        for entry in entries:
            profile = entry.get("profile")
            if profile not in ("interaction", "interaction-shadow-stock"):
                continue
            key = (profile, entry.get("renderApi"))
            if key in indexed:
                failures.append(f"duplicate {key[0]}/{key[1]}")
                continue
            indexed[key] = entry

        for profile, backend in sorted(expected_matrix_keys - set(indexed)):
            failures.append(f"missing {profile}/{backend}")
        for profile, backend in sorted(
            set(indexed) - expected_matrix_keys,
            key=lambda item: (str(item[0]), str(item[1])),
        ):
            failures.append(f"unexpected {profile}/{backend}")

        for profile, backend in sorted(expected_matrix_keys & set(indexed)):
            command = str(indexed[(profile, backend)].get("command", ""))
            for required_option in (
                f"--render-api {backend}",
                "--reference-dir",
                "--require-references",
                "--image-rms-threshold 0",
                "--image-max-threshold 0",
                "--difference-reference-dir",
            ):
                if required_option not in command:
                    failures.append(
                        f"{profile}/{backend} missing option {required_option}"
                    )
            if (
                f"{profile}\\{backend}\\classic\\savepaths" not in command
                or f"{profile}\\{backend}\\shadows-off\\savepaths"
                not in command
            ):
                failures.append(
                    f"{profile}/{backend} references are not backend-scoped savepaths"
                )
        return failures

    matrix_entries = list(matrix.GAMEPLAY_BENCHMARK_HARNESS)
    matrix_failures = interaction_matrix_failures(matrix_entries)
    if matrix_failures:
        raise AssertionError(
            "Renderer validation matrix must register exactly one GL and Vulkan "
            "gate for each interaction profile: " + "; ".join(matrix_failures)
        )

    missing_fixture = [
        entry
        for entry in matrix_entries
        if (entry.get("profile"), entry.get("renderApi"))
        != ("interaction", "gl")
    ]
    if "missing interaction/gl" not in interaction_matrix_failures(missing_fixture):
        raise AssertionError(
            "Interaction matrix guard must reject a missing profile/backend pair"
        )

    duplicate_fixture = list(matrix_entries)
    duplicate_fixture.append(
        dict(
            next(
                entry
                for entry in matrix_entries
                if (entry.get("profile"), entry.get("renderApi"))
                == ("interaction", "gl")
            )
        )
    )
    if "duplicate interaction/gl" not in interaction_matrix_failures(
        duplicate_fixture
    ):
        raise AssertionError(
            "Interaction matrix guard must reject a duplicate profile/backend pair"
        )

    foundation = next(
        case
        for case in matrix.build_safe_cases(("auto", "gl33", "gl43", "gl45"))
        if case["id"] == "renderer-foundation-selftests"
    )
    foundation_args = foundation["args"]
    if not any(
        foundation_args[index : index + 3] == ["+set", "r_renderApi", "gl"]
        for index in range(len(foundation_args) - 2)
    ) or not foundation.get("preservesConfig"):
        raise AssertionError(
            "Foundation self-tests must force OpenGL without persisting the archived API choice"
        )


def validate_runtime_failure_gates() -> None:
    gameplay = load_test_module(
        "tools/tests/renderer_gameplay_benchmark.py",
        "openq4_renderer_gameplay_benchmark_contract",
    )
    matrix = load_test_module(
        "tools/tests/renderer_validation_matrix.py",
        "openq4_renderer_validation_matrix_contract",
    )

    signature_lines = {
        "binaryImageCacheWrite": "^3WARNING: ^1idBinaryImage: Could not open generated cache 'generated/images/fixture.bimage' or compact fallback 'generated/images/_compact/v1/fixture.bimage'",
        "expandedLoadscreenPublish": "^3WARNING: ^1Could not publish expanded loading background 'guis/assets/generated/loadscreens/fixture_1820x1024.tga'; using the source levelshot",
        "vulkanValidation": "Vulkan validation: descriptor binding mismatch",
        "vulkanVuid": "Validation ID VUID-vkCmdDrawIndexed-commandBuffer-recording",
        "vulkanCallFailed": "Vulkan: vkCreateGraphicsPipelines failed (-3)",
        "fatal": "Fatal Error: renderer bootstrap stopped",
        "errorLine": "ERROR: render target creation stopped",
    }
    failure_text = "\n".join(signature_lines.values())
    native_fatal = "FATAL: renderer bootstrap stopped"
    benign_vk_result = "Vulkan: swapchain returned VK_ERROR_OUT_OF_DATE_KHR; retry scheduled"
    legacy_binary_image_warning = (
        "^3WARNING: ^1idBinaryImage: Could not open file "
        "'generated/images/legacy-fixture.bimage'"
    )
    binary_image_near_miss = (
        "idBinaryImage: Could not open generated cache 'generated/images/fixture.bimage' "
        "or compact fallback 'generated/images/_compact/v1/fixture.bimage'"
    )
    expanded_loadscreen_near_miss = (
        "Could not publish expanded loading background "
        "'guis/assets/generated/loadscreens/fixture_1820x1024.tga'; using the source levelshot"
    )

    for module, counter_name, context in (
        (gameplay, "warning_counts", "gameplay benchmark"),
        (matrix, "count_warning_signatures", "renderer validation matrix"),
    ):
        counter = getattr(module, counter_name)
        counts = counter(failure_text)
        for signature in signature_lines:
            if counts.get(signature) != 1:
                raise AssertionError(
                    f"{context} must count synthetic {signature} exactly once; got {counts.get(signature)!r}"
                )

        diagnostics, omitted = module.collect_failure_diagnostics(
            (("synthetic.log", failure_text),)
        )
        if omitted != 0:
            raise AssertionError(f"{context} unexpectedly omitted synthetic failure diagnostics")
        for signature, source_line in signature_lines.items():
            matches = [
                item
                for item in diagnostics
                if signature in item["signatures"] and item["text"] == source_line
            ]
            if len(matches) != 1:
                raise AssertionError(
                    f"{context} must preserve the exact source line for {signature}; got {matches!r}"
                )

        benign_counts = counter(benign_vk_result)
        if benign_counts.get("errorLine") != 0:
            raise AssertionError(
                f"{context} must not treat an embedded VK_ERROR_* result name as an engine ERROR line"
            )
        native_fatal_counts = counter(native_fatal)
        if native_fatal_counts.get("fatal") != 1:
            raise AssertionError(
                f"{context} must recognize the engine-native FATAL: prefix"
            )
        native_diagnostics, native_omitted = module.collect_failure_diagnostics(
            (("native-fatal.log", native_fatal),)
        )
        if native_omitted != 0 or not any(
            "fatal" in item["signatures"] and item["text"] == native_fatal
            for item in native_diagnostics
        ):
            raise AssertionError(
                f"{context} must preserve the engine-native FATAL: diagnostic line"
            )
        if counter(legacy_binary_image_warning).get("binaryImageCacheWrite") != 1:
            raise AssertionError(
                f"{context} must retain the legacy binary-image cache warning signature"
            )
        if counter(binary_image_near_miss).get("binaryImageCacheWrite") != 0:
            raise AssertionError(
                f"{context} must require WARNING: for binary-image cache write failures"
            )
        if counter(expanded_loadscreen_near_miss).get("expandedLoadscreenPublish") != 0:
            raise AssertionError(
                f"{context} must require WARNING: for expanded-loadscreen publication failures"
            )

    checks_ok, missing = matrix.evaluate_checks(
        failure_text,
        [],
        matrix.count_warning_signatures(failure_text),
    )
    if checks_ok:
        raise AssertionError("Renderer validation matrix must fail when a fatal runtime signature is present")
    for signature in signature_lines:
        expected = f"warning signature: {signature}=1"
        if expected not in missing:
            raise AssertionError(f"Renderer validation matrix did not gate {expected!r}")
    benign_ok, benign_missing = matrix.evaluate_checks(
        benign_vk_result,
        [],
        matrix.count_warning_signatures(benign_vk_result),
    )
    if not benign_ok:
        raise AssertionError(
            f"Benign Vulkan result enum should not fail the validation matrix: {benign_missing!r}"
        )

    # Exercise the full gameplay-role evaluator, not only its regex table:
    # with all ordinary role evidence present, the signatures alone fail the
    # role, while a benign Vulkan result enum remains a pass.
    tmp_root = ROOT / ".tmp"
    tmp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="vulkan-shadow-contract-", dir=tmp_root) as temp_name:
        temp = Path(temp_name)
        savepath = temp / "save"
        log_path = savepath / "baseoq4" / "logs" / "contract.log"
        screenshot_rel = "screenshots/contract.tga"
        screenshot_path = savepath / "baseoq4" / screenshot_rel
        stdout_path = temp / "stdout.log"
        stderr_path = temp / "stderr.log"
        log_path.parent.mkdir(parents=True)
        screenshot_path.parent.mkdir(parents=True)
        stdout_path.write_text("", encoding="utf-8")
        stderr_path.write_text("", encoding="utf-8")
        write_rgb_tga(
            screenshot_path, gameplay.BUDGET_WIDTH, gameplay.BUDGET_HEIGHT, 0
        )

        spec = gameplay.RunSpec(
            case_id="vulkan-shadow-contract",
            mode="SP",
            map_name="game/contract",
            budget_map_name="game/contract",
            purpose="synthetic failure-gate coverage",
            path_name="spawn-static",
            tier="auto",
            maxfps="0",
            swap_interval="0",
            display_mode="windowed",
            shadow_preset="mapped",
            renderer="vk",
            render_api="vk",
        )

        def evaluate(log_body: str) -> dict[str, object]:
            log_path.write_text(
                "Selected renderer tier: Vulkan\n"
                f"MODE: -1, {gameplay.BUDGET_WIDTH} x {gameplay.BUDGET_HEIGHT} "
                f"windowed hz:N/A\n{log_body}\n",
                encoding="utf-8",
            )
            return gameplay.evaluate_role_result(
                spec=spec,
                role="client",
                exit_code=0,
                timed_out=False,
                elapsed_seconds=1.0,
                savepath=savepath,
                log_name="contract.log",
                stdout_path=stdout_path,
                stderr_path=stderr_path,
                screenshot_rel=screenshot_rel,
                reference_dir=None,
                rms_threshold=0.0,
                max_threshold=0,
                require_reference=False,
                require_benchmark=False,
            )

        failure_result = evaluate(failure_text)
        if failure_result["status"] != "fail":
            raise AssertionError("Gameplay role must fail when fatal Vulkan diagnostics are present")
        for signature in signature_lines:
            expected = f"{signature}=1"
            if expected not in failure_result["missing"]:
                raise AssertionError(f"Gameplay role did not gate {expected!r}")
        if len(failure_result["failureDiagnostics"]) < len(signature_lines):
            raise AssertionError("Gameplay role did not retain all synthetic failure diagnostic lines")

        benign_result = evaluate(benign_vk_result)
        if benign_result["warnings"]["errorLine"] != 0:
            raise AssertionError("Gameplay role classified VK_ERROR_* as an engine ERROR line")
        if benign_result["status"] != "pass":
            raise AssertionError(
                f"Benign Vulkan result enum should not fail the gameplay role: {benign_result['missing']!r}"
            )


def validate_receiver_ownership_split() -> None:
    header = read("src/renderer/Vulkan/vk_ShadowMap.h")
    require_order(
        header,
        (
            "typedef enum vkShadowReceiverPass_e {",
            "VK_SHADOW_RECEIVER_LOCAL = 0,",
            "VK_SHADOW_RECEIVER_GLOBAL,",
            "VK_SHADOW_RECEIVER_PASS_COUNT",
            "} vkShadowReceiverPass_t;",
        ),
        "Vulkan shadow receiver-pass enum",
    )
    require_compact(
        header,
        "vkShadowPassState_t passes[ VK_SHADOW_RECEIVER_PASS_COUNT ];",
        "per-light ownership resources",
    )
    require_compact(
        header,
        """int VK_ShadowMap_PrepareViewLights( const viewDef_t *viewDef,
                bool stencilFallbackAvailable );""",
        "active-target stencil availability contract",
    )

    shadow_map = read("src/renderer/Vulkan/vk_ShadowMap.cpp")
    caster_gate = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_PassHasCasters(",
        "ownership-specific caster gate",
    )
    require_order(
        caster_gate,
        (
            "vLight->globalShadowMapCasters != NULL",
            "vLight->globalShadowMapDynamicCasters != NULL",
            "return true;",
            "receiverPass == VK_SHADOW_RECEIVER_GLOBAL",
            "vLight->localShadowMapCasters != NULL",
            "vLight->localShadowMapDynamicCasters != NULL",
        ),
        "ownership-specific caster gate",
    )

    prepare = braced_body(
        shadow_map,
        "int VK_ShadowMap_PrepareViewLights(",
        "shadow-map light preparation",
    )
    require_compact(
        prepare,
        """const bool passNeeded[ VK_SHADOW_RECEIVER_PASS_COUNT ] = {
            vLight->localInteractions != NULL,
            vLight->globalInteractions != NULL || hasTranslucentReceivers
        };""",
        "ownership-specific receiver admission",
    )
    require_compact(
        prepare,
        """const bool passHasCasters[ VK_SHADOW_RECEIVER_PASS_COUNT ] = {
            VK_ShadowMap_PassHasCasters( vLight, VK_SHADOW_RECEIVER_LOCAL ),
            VK_ShadowMap_PassHasCasters( vLight, VK_SHADOW_RECEIVER_GLOBAL )
        };""",
        "ownership-specific caster admission",
    )
    safe_alias = compact(
        """receiverPass == VK_SHADOW_RECEIVER_GLOBAL
           && !VK_ShadowMap_HasLocalCasters( vLight )
           && entry.passes[ VK_SHADOW_RECEIVER_LOCAL ].valid"""
    )
    if compact(prepare).count(safe_alias) < 2:
        raise AssertionError(
            "Projected and point shadow resources must alias LOCAL to GLOBAL only when no local casters exist"
        )

    render = braced_body(
        shadow_map,
        "bool VK_ShadowMap_RenderAtlas(",
        "shadow-map caster rendering",
    )
    # Projected ownership passes route their chains through the shared
    # static/dynamic selector so the compose split lives in one place; the
    # point cube pass still walks the four chains itself, because a cube has
    # no composition path.
    pass_casters = braced_body(
        shadow_map,
        "static int VK_ShadowMap_DrawPassCasters(",
        "projected ownership caster chain selection",
    )
    for token in (
        "vLight->globalShadowMapCasters",
        "vLight->globalShadowMapDynamicCasters",
        "vLight->localShadowMapCasters",
        "vLight->localShadowMapDynamicCasters",
    ):
        if pass_casters.count(token) < 1 or render.count(token) < 1:
            raise AssertionError(
                f"Projected and point ownership passes must both retain caster chain {token!r}"
            )
    for global_only_chain in (
        "vLight->localShadowMapCasters",
        "vLight->localShadowMapDynamicCasters",
    ):
        if f"globalOwnership && drawStatic" not in pass_casters or (
            f"globalOwnership && drawDynamic" not in pass_casters
        ):
            raise AssertionError(
                "Projected GLOBAL resources must gate the local caster chains on ownership"
            )
        if global_only_chain not in pass_casters:
            raise AssertionError(
                f"Projected GLOBAL resources must add local caster chain {global_only_chain!r}"
            )
    if compact(render).count(
        compact("receiverPass == VK_SHADOW_RECEIVER_GLOBAL")
    ) < 1:
        raise AssertionError(
            "Point GLOBAL resources must add local caster chains"
        )

    interactions = read("src/renderer/Vulkan/vk_Interactions.cpp")
    require_compact(
        interactions,
        """const bool stencilFallbackAvailable =
                activeTargetHasStencil &&
                interPass.pipelineStencilShadow != VK_NULL_HANDLE;
            if ( VK_ShadowMap_PrepareViewLights(
                    viewDef, stencilFallbackAvailable ) > 0 )""",
        "active-target stencil availability handoff",
    )
    stencil_pass = braced_body(
        interactions,
        "static bool VK_StencilShadowPass(",
        "runtime stencil-volume submission",
    )
    require_order(
        stencil_pass,
        (
            "bool complete = true;",
            "interPass.volumeSkipCount++;",
            "complete = false;",
            "return complete;",
        ),
        "runtime stencil-volume completion result",
    )
    if stencil_pass.count("complete = false;") < 3:
        raise AssertionError(
            "Every classic and packed stencil geometry failure must invalidate the runtime fallback"
        )

    draw_lights = braced_body(
        interactions,
        "void VK_Interactions_DrawLights(",
        "Vulkan light interactions",
    )
    require_order(
        draw_lights,
        (
            """localShadowState = VK_ShadowMap_PassState(
                shadowState, VK_SHADOW_RECEIVER_LOCAL );""",
            """globalShadowState = VK_ShadowMap_PassState(
                shadowState, VK_SHADOW_RECEIVER_GLOBAL );""",
            "localEmptyFallback ? NULL : localShadowState",
            "VK_DrawInteractionChain( vLight->localInteractions );",
            "globalEmptyFallback ? NULL : globalShadowState",
            "VK_DrawInteractionChain( vLight->globalInteractions );",
        ),
        "ownership-specific receiver selection",
    )


def validate_shadow_depth_format_selection() -> None:
    device_header = read("src/renderer/Vulkan/VulkanDevice.h")
    for field in (
        "VkFormat shadowDepthFormat;",
        "bool shadowDepthHasStencil;",
        "bool shadowDepthFilterLinear;",
    ):
        require_compact(device_header, field, "Vulkan shadow depth capabilities")

    device = read("src/renderer/Vulkan/VulkanDevice.cpp")
    stencil_classifier = braced_body(
        device,
        "static bool VK_Device_DepthFormatHasStencil(",
        "depth/stencil format classification",
    )
    require_order(
        stencil_classifier,
        (
            "case VK_FORMAT_D16_UNORM_S8_UINT:",
            "case VK_FORMAT_D24_UNORM_S8_UINT:",
            "case VK_FORMAT_D32_SFLOAT_S8_UINT:",
            "return true;",
            "default:",
            "return false;",
        ),
        "depth/stencil format classification",
    )

    selector = braced_body(
        device,
        "static void VK_Device_SelectShadowDepthFormat(",
        "shadow depth-format selection",
    )
    require_order(
        selector,
        (
            "vkCtx.shadowDepthFormat = VK_FORMAT_UNDEFINED;",
            "VK_FORMAT_D24_UNORM_S8_UINT,",
            "VK_FORMAT_D32_SFLOAT_S8_UINT,",
            "VK_FORMAT_D32_SFLOAT,",
            "VK_FORMAT_X8_D24_UNORM_PACK32,",
            "VK_FORMAT_D16_UNORM,",
            "VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT",
            "VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT",
            "VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT",
            "VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT",
            "VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT",
            "vkGetPhysicalDeviceFormatProperties2(",
            # VkFormatProperties3 is core 1.3 and every implementation at the
            # renderer's API floor populates it, but an implementation that
            # left it zeroed would silently reject every candidate and disable
            # shadow maps, so the selector falls back to the 1.0 flags.
            "VkFormatFeatureFlags2 optimalFeatures = props3.optimalTilingFeatures;",
            "if ( optimalFeatures == 0 ) {",
            "props2.formatProperties.optimalTilingFeatures",
            "VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT",
            "optimalFeatures |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT;",
            "( optimalFeatures & requiredFeatures ) != requiredFeatures",
            "VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT",
            "nearestFallback = candidates[ i ];",
            "vkCtx.shadowDepthFormat = candidates[ i ];",
            "vkCtx.shadowDepthHasStencil = VK_Device_DepthFormatHasStencil( candidates[ i ] );",
            "vkCtx.shadowDepthFilterLinear = true;",
        ),
        "shadow depth-format selection",
    )
    require_compact(
        selector,
        """if ( vkCtx.shadowDepthFormat == VK_FORMAT_UNDEFINED
            && nearestFallback != VK_FORMAT_UNDEFINED )""",
        "nearest-filter shadow depth fallback",
    )
    require_compact(
        selector,
        "vkCtx.shadowDepthHasStencil = nearestFallbackHasStencil;",
        "nearest-filter shadow stencil metadata",
    )

    device_init = braced_body(device, "bool VK_Device_Init(", "Vulkan device initialization")
    require_order(
        device_init,
        (
            "vkCtx.physicalDevice = devices[ chosenDevice ];",
            "VK_Device_SelectShadowDepthFormat();",
        ),
        "shadow depth probing after physical-device selection",
    )

    executor = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    for marker, context in (
        ("VkPipeline VK_Exec_CasterPipeline(", "projected shadow caster pipeline"),
        ("VkPipeline VK_Exec_PointCasterPipeline(", "point shadow caster pipeline"),
    ):
        caster_pipeline = braced_body(executor, marker, context)
        require_compact(
            caster_pipeline,
            "target.depthFormat = vkCtx.shadowDepthFormat;",
            context,
        )
        require_compact(
            caster_pipeline,
            """target.stencilFormat = vkCtx.shadowDepthHasStencil
                ? vkCtx.shadowDepthFormat : VK_FORMAT_UNDEFINED;""",
            context,
        )

    shadow_map = read("src/renderer/Vulkan/vk_ShadowMap.cpp")
    aspect_mask = braced_body(
        shadow_map,
        "static VkImageAspectFlags VK_ShadowMap_DepthAspectMask(",
        "shadow attachment aspect selection",
    )
    require_compact(
        aspect_mask,
        """VK_IMAGE_ASPECT_DEPTH_BIT |
            ( vkCtx.shadowDepthHasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0 )""",
        "shadow attachment aspect selection",
    )

    resources = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_EnsureResources(",
        "shadow-map resource creation",
    )
    require_order(
        resources,
        (
            "vkCtx.shadowDepthFormat == VK_FORMAT_UNDEFINED",
            "ici.format = vkCtx.shadowDepthFormat;",
            """ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                | VK_IMAGE_USAGE_SAMPLED_BIT
                | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                | VK_IMAGE_USAGE_TRANSFER_DST_BIT;""",
            "ivci.subresourceRange.aspectMask = VK_ShadowMap_DepthAspectMask();",
            "ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;",
            "vkCtx.shadowDepthFilterLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST",
            "sci.compareEnable = VK_TRUE;",
        ),
        "shadow-map resource creation",
    )

    render = braced_body(
        shadow_map,
        "bool VK_ShadowMap_RenderAtlas(",
        "shadow-map caster rendering",
    )
    if compact(render).count(
        compact("ri.pStencilAttachment = vkCtx.shadowDepthHasStencil ? &depth : NULL;")
    ) < 2:
        raise AssertionError(
            "Projected and point shadow rendering must both omit stencil attachment metadata for depth-only formats"
        )


def validate_csm_atlas_and_receiver_contract() -> None:
    header = read("src/renderer/Vulkan/vk_ShadowMap.h")
    require(header, '#include "../ShadowMapProjected.h"', "Vulkan CSM shared state")
    require_compact(
        header,
        "float atlasRects[ SHADOWMAP_PROJECTED_MAX_CASCADES ][ 4 ];",
        "per-cascade atlas rectangles",
    )
    require_compact(
        header,
        "shadowMapProjectedLightState_t projectedState;",
        "full projected-light CSM state",
    )

    classification_source = read("src/renderer/ShadowMapClassification.cpp")
    classification = braced_body(
        classification_source,
        "shadowMapLightClassification_t R_ClassifyShadowMapLight(",
        "shared shadow-map light classification",
    )
    require_order(
        classification,
        (
            "if ( classification.csmEnabled )",
            "classification.cascadeCount = requestedCascadeCount;",
            "classification.atlasDiv = 2;",
            "classification.tileCount = requestedCascadeCount;",
        ),
        "CSM 2x2 atlas classification",
    )

    shadow_map = read("src/renderer/Vulkan/vk_ShadowMap.cpp")
    allocator = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_AllocTileBlock(",
        "contiguous shadow-atlas block allocator",
    )
    require_order(
        allocator,
        (
            "blockSize <= 0 || blockSize > vkShadow.atlasSize",
            "vkShadow.nextTileX + blockSize > vkShadow.atlasSize",
            "vkShadow.nextTileY += vkShadow.nextTileRowHeight;",
            "vkShadow.nextTileY + blockSize > vkShadow.atlasSize",
            "tileX = vkShadow.nextTileX;",
            "tileY = vkShadow.nextTileY;",
            "vkShadow.nextTileX += blockSize;",
            "vkShadow.nextTileRowHeight = Max( vkShadow.nextTileRowHeight, blockSize );",
        ),
        "contiguous shadow-atlas block allocator",
    )

    projected_alloc = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_AllocateProjectedPass(",
        "projected cascade-block allocation",
    )
    require_order(
        projected_alloc,
        (
            "const int atlasDiv = idMath::ClampInt( 1, 2, light.projectedState.atlasDiv );",
            "const int blockSize = light.tileSize * atlasDiv;",
            "VK_ShadowMap_AllocTileBlock( blockSize, tileX, tileY )",
            "const int cascadeCount = idMath::ClampInt( 1, SHADOWMAP_PROJECTED_MAX_CASCADES, light.projectedState.cascadeCount );",
            "for ( int cascadeIndex = 0 ; cascadeIndex < cascadeCount ; cascadeIndex++ )",
            "const int cascadeX = cascadeIndex % atlasDiv;",
            "const int cascadeY = cascadeIndex / atlasDiv;",
            "const int cascadeTileX = tileX + cascadeX * light.tileSize;",
            "const int cascadeTileY = tileY + cascadeY * light.tileSize;",
            "pass.atlasRects[ cascadeIndex ][ 0 ] = (float)cascadeTileX * invAtlas;",
            "pass.atlasRects[ cascadeIndex ][ 1 ] = (float)( cascadeTileY + light.tileSize ) * invAtlas;",
            "pass.atlasRects[ cascadeIndex ][ 2 ] = (float)( cascadeTileX + light.tileSize ) * invAtlas;",
            "pass.atlasRects[ cascadeIndex ][ 3 ] = (float)cascadeTileY * invAtlas;",
        ),
        "projected 1x1/2x2 cascade tile placement",
    )

    prepare = braced_body(
        shadow_map,
        "int VK_ShadowMap_PrepareViewLights(",
        "projected CSM light preparation",
    )
    require_order(
        prepare,
        (
            "const int requestedAtlasDiv = idMath::ClampInt( 1, 2, classification.atlasDiv );",
            "const int maxTileSize = vkShadow.atlasSize / requestedAtlasDiv;",
            "R_BuildShadowMapProjectedLightState( vLight, viewDef, tileSize, projectedState );",
            "projectedState.cascadeCount > SHADOWMAP_PROJECTED_MAX_CASCADES",
            "projectedState.atlasDiv < 1 || projectedState.atlasDiv > 2",
            "projectedState.tileSize * projectedState.atlasDiv > vkShadow.atlasSize",
            "entry.tileSize = projectedState.tileSize;",
            "entry.projectedState = projectedState;",
        ),
        "projected CSM admission and state retention",
    )

    render = braced_body(
        shadow_map,
        "bool VK_ShadowMap_RenderAtlas(",
        "projected cascade caster rendering",
    )
    require_order(
        render,
        (
            "const int cascadeCount = idMath::ClampInt( 1, SHADOWMAP_PROJECTED_MAX_CASCADES, light.projectedState.cascadeCount );",
            "const int atlasDiv = idMath::ClampInt( 1, 2, light.projectedState.atlasDiv );",
            "for ( int cascadeIndex = 0 ; cascadeIndex < cascadeCount ; cascadeIndex++ )",
            "const int cascadeTileX = pass.tileX + ( cascadeIndex % atlasDiv ) * light.tileSize;",
            "const int cascadeTileY = pass.tileY + ( cascadeIndex / atlasDiv ) * light.tileSize;",
            "viewport.x = (float)cascadeTileX;",
            "viewport.y = (float)( cascadeTileY + light.tileSize );",
            "scissor.offset.x = cascadeTileX;",
            "scissor.offset.y = cascadeTileY;",
            """VK_ShadowMap_DrawPassCasters( ctx, light, receiverPass,
                cascadeIndex, pass.composeDynamic
                    ? VK_SHADOW_CHAINS_STATIC_ONLY
                    : VK_SHADOW_CHAINS_ALL )""",
            "VK_ShadowMap_InvalidatePassResource( light, receiverPass );",
        ),
        "complete per-cascade ownership rendering",
    )

    # The selector is the single place the four chains are split into the
    # GL SHADOWMAP_RENDER_STATIC_ONLY / _COMPOSE_DYNAMIC / full sets.
    pass_casters = braced_body(
        shadow_map,
        "static int VK_ShadowMap_DrawPassCasters(",
        "projected ownership caster chain selection",
    )
    require_order(
        pass_casters,
        (
            "const bool drawStatic = select != VK_SHADOW_CHAINS_DYNAMIC_ONLY;",
            "const bool drawDynamic = select != VK_SHADOW_CHAINS_STATIC_ONLY;",
            "const bool globalOwnership = receiverPass == VK_SHADOW_RECEIVER_GLOBAL;",
            "if ( drawStatic )",
            "VK_ShadowMap_DrawCasterChain( ctx, light, cascadeIndex, vLight->globalShadowMapCasters )",
            "if ( drawDynamic )",
            "VK_ShadowMap_DrawCasterChain( ctx, light, cascadeIndex, vLight->globalShadowMapDynamicCasters )",
            "if ( globalOwnership && drawStatic )",
            "VK_ShadowMap_DrawCasterChain( ctx, light, cascadeIndex, vLight->localShadowMapCasters )",
            "if ( globalOwnership && drawDynamic )",
            "VK_ShadowMap_DrawCasterChain( ctx, light, cascadeIndex, vLight->localShadowMapDynamicCasters )",
        ),
        "static/dynamic caster chain selection",
    )

    interactions = read("src/renderer/Vulkan/vk_Interactions.cpp")
    shadow_slice = braced_body(
        interactions,
        "static int VK_Inter_WriteShadowSlice(",
        "projected CSM receiver block writer",
    )
    require_order(
        shadow_slice,
        (
            "const shadowMapProjectedLightState_t &projected = state->projectedState;",
            "for ( int cascadeIndex = 0 ; cascadeIndex < cascadeCount ; cascadeIndex++ )",
            "projected.clipPlanes[ cascadeIndex ][ 0 ]",
            "block.shadowRow0[ cascadeIndex ]",
            "projected.clipPlanes[ cascadeIndex ][ 1 ]",
            "block.shadowRow1[ cascadeIndex ]",
            "projected.clipPlanes[ cascadeIndex ][ 2 ]",
            "block.shadowRow2[ cascadeIndex ]",
            "projected.clipPlanes[ cascadeIndex ][ 3 ]",
            "block.shadowRow3[ cascadeIndex ]",
            "memcpy( block.atlasRects, passState->atlasRects, sizeof( block.atlasRects ) );",
            "memcpy( block.splitDepths, projected.splitDepths, sizeof( block.splitDepths ) );",
            "block.viewDepthRow[ 0 ] = -modelView[ 2 ];",
            "block.viewDepthRow[ 1 ] = -modelView[ 6 ];",
            "block.viewDepthRow[ 2 ] = -modelView[ 10 ];",
            "block.viewDepthRow[ 3 ] = -modelView[ 14 ];",
            "block.biasParams[ 2 ] = idMath::ClampFloat( 0.0f, 0.5f, r_shadowMapCascadeBlend.GetFloat() );",
            "block.biasParams[ 3 ] = (float)cascadeCount;",
        ),
        "four-cascade receiver coordinate ABI",
    )

    vertex_shader = read("src/renderer/Vulkan/shaders/interaction_shadow.vert")
    vertex_main = braced_body(
        vertex_shader,
        "void main()",
        "projected shadow receiver vertex shader",
    )
    for cascade_index in range(4):
        require(
            vertex_shader,
            f"layout(location = {8 + cascade_index}) out vec4 vShadowCoord{cascade_index};",
            "four projected receiver coordinates",
        )
        require_compact(
            vertex_main,
            f"vShadowCoord{cascade_index} = BuildShadowCoord(position, shadowNormal, shadowSinTheta, {cascade_index});",
            "four projected receiver coordinates",
        )
    require(
        vertex_shader,
        "layout(location = 14) out float vViewDepth;",
        "projected receiver view depth",
    )
    require_compact(
        vertex_main,
        "vViewDepth = max(dot(position, shadow.viewDepthRow), 0.0);",
        "projected receiver view depth",
    )

    fragment_shader = read("src/renderer/Vulkan/shaders/interaction_shadow.frag")
    coord_select = braced_body(
        fragment_shader,
        "vec4 ShadowCoordByIndex(",
        "projected receiver coordinate selection",
    )
    atlas_select = braced_body(
        fragment_shader,
        "vec4 AtlasRectByIndex(",
        "projected receiver atlas selection",
    )
    for cascade_index in range(4):
        require(
            coord_select,
            f"vShadowCoord{cascade_index}",
            "projected receiver coordinate selection",
        )
        require(
            atlas_select,
            f"shadow.atlasRects[{cascade_index}]",
            "projected receiver atlas selection",
        )

    cascade_select = braced_body(
        fragment_shader,
        "int SelectShadowCascade(",
        "view-depth cascade selection",
    )
    require_order(
        cascade_select,
        (
            "int interiorSplitCount = ShadowCascadeCount() - 1;",
            "viewDepth < shadow.splitDepths.x",
            "return 0;",
            "viewDepth < shadow.splitDepths.y",
            "return 1;",
            "viewDepth < shadow.splitDepths.z",
            "return 2;",
            "return 3;",
        ),
        "view-depth cascade selection",
    )

    shadow_factor = braced_body(
        fragment_shader,
        "float SampleShadowFactor()",
        "cascade split-band blending",
    )
    require_order(
        shadow_factor,
        (
            "int cascadeIndex = SelectShadowCascade(vViewDepth);",
            "float shadowFactor = SampleCascadeByIndex(cascadeIndex);",
            "int lastInteriorIndex = ShadowCascadeCount() - 2;",
            "float cascadeBlend = shadow.biasParams.z;",
            "float previousSplit = cascadeIndex == 0 ? 0.0",
            "float currentSplit = CascadeComponent(shadow.splitDepths, cascadeIndex);",
            "(currentSplit - previousSplit) * cascadeBlend",
            "float blendStart = currentSplit - blendWidth;",
            "float blend = clamp((vViewDepth - blendStart) / blendWidth, 0.0, 1.0);",
            "return mix(shadowFactor, SampleCascadeByIndex(cascadeIndex + 1), blend);",
        ),
        "cascade split-band blending",
    )


def validate_shadow_descriptor_abi() -> None:
    executor = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    require(
        executor,
        "static const int VK_SHADOW_UNIFORM_SLICE_BYTES = 512;",
        "set-7 512-byte shadow UBO ABI",
    )

    init = braced_body(executor, "static bool VK_GuiExecutor_Init(", "Vulkan executor initialization")
    require_order(
        init,
        (
            "vkCtx.deviceProperties.limits.maxUniformBufferRange < VK_SHADOW_UNIFORM_SLICE_BYTES",
            "VK_Exec_UniformSliceAlignment( VK_SHADOW_UNIFORM_SLICE_BYTES ) == 0",
            "shadowBindings[ 0 ].binding = 0;",
            "shadowBindings[ 0 ].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;",
            "shadowBindings[ 1 ].binding = 1;",
            "shadowBindings[ 1 ].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;",
            "shadowBindings[ 2 ].binding = 2;",
            "shadowBindings[ 2 ].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;",
            "dslci.bindingCount = 3;",
            "interactionSetLayouts[ 7 ] = vkExec.shadowSetLayout;",
            "bufferInfo.range = VK_SHADOW_UNIFORM_SLICE_BYTES;",
            "write.dstBinding = 1;",
        ),
        "set-7 compare/UBO/raw descriptor layout",
    )

    shadow_alloc = braced_body(
        executor,
        "int VK_Exec_ShadowUniformAlloc(",
        "set-7 shadow UBO allocation",
    )
    require_order(
        shadow_alloc,
        (
            "bytes > VK_SHADOW_UNIFORM_SLICE_BYTES",
            "VK_Exec_UniformSliceAlignment( VK_SHADOW_UNIFORM_SLICE_BYTES )",
            "VK_Ring_Alloc(",
        ),
        "set-7 512-byte shadow UBO allocation",
    )

    descriptor_getter = braced_body(
        executor,
        "VkDescriptorSet VK_Exec_ShadowDescriptorSet(",
        "atlas shadow descriptor publication",
    )
    require_compact(
        descriptor_getter,
        "return vkExec.shadowSetsHaveAtlas ? vkExec.shadowSets[ vkExec.frameSlot ] : VK_NULL_HANDLE;",
        "atlas shadow descriptor publication",
    )

    atlas_descriptors = braced_body(
        executor,
        "bool VK_Exec_UpdateShadowAtlasDescriptors(",
        "atlas compare/raw descriptors",
    )
    require_order(
        atlas_descriptors,
        (
            "vkExec.shadowSetsHaveAtlas = false;",
            "view == VK_NULL_HANDLE",
            "compareSampler == VK_NULL_HANDLE",
            "rawSampler == VK_NULL_HANDLE",
            "imageInfos[ 0 ].sampler = compareSampler;",
            "imageInfos[ 1 ].sampler = rawSampler;",
            "writes[ writeIndex ].dstBinding = writeIndex == 0 ? 0 : 2;",
            "vkUpdateDescriptorSets( vkCtx.device, 2, writes, 0, NULL );",
            "vkExec.shadowSetsHaveAtlas = true;",
        ),
        "fail-closed atlas compare/raw descriptor publication",
    )

    cube_descriptors = braced_body(
        executor,
        "bool VK_Exec_CreateShadowCubeSets(",
        "point cube compare/raw descriptors",
    )
    require_order(
        cube_descriptors,
        (
            "compareSampler == VK_NULL_HANDLE",
            "rawSampler == VK_NULL_HANDLE",
            "bufferInfo.range = VK_SHADOW_UNIFORM_SLICE_BYTES;",
            "ringWrite.dstBinding = 1;",
            "imageInfos[ 0 ].sampler = compareSampler;",
            "imageInfos[ 1 ].sampler = rawSampler;",
            "writes[ writeIndex ].dstBinding = writeIndex == 0 ? 0 : 2;",
            "vkUpdateDescriptorSets( vkCtx.device, 2, writes, 0, NULL );",
        ),
        "fail-closed point cube compare/raw descriptors",
    )

    shadow_map = read("src/renderer/Vulkan/vk_ShadowMap.cpp")
    resources = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_EnsureResources(",
        "atlas compare/raw sampler resources",
    )
    require_compact(
        resources,
        """vkShadow.atlasImage != VK_NULL_HANDLE
            && vkShadow.atlasSize == wantedSize
            && vkShadow.compareSampler != VK_NULL_HANDLE
            && vkShadow.rawSampler != VK_NULL_HANDLE""",
        "live atlas requires both sampler families",
    )
    compare_sampler = braced_body(
        resources,
        "if ( vkShadow.compareSampler == VK_NULL_HANDLE )",
        "shadow comparison sampler",
    )
    require_order(
        compare_sampler,
        (
            "sci.compareEnable = VK_TRUE;",
            "sci.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;",
            "vkCreateSampler( vkCtx.device, &sci, NULL, &vkShadow.compareSampler )",
        ),
        "shadow comparison sampler",
    )
    raw_sampler = braced_body(
        resources,
        "if ( vkShadow.rawSampler == VK_NULL_HANDLE )",
        "shadow raw-depth sampler",
    )
    require_order(
        raw_sampler,
        (
            "sci.magFilter = VK_FILTER_NEAREST;",
            "sci.minFilter = VK_FILTER_NEAREST;",
            "sci.compareEnable = VK_FALSE;",
            "vkCreateSampler( vkCtx.device, &sci, NULL, &vkShadow.rawSampler )",
        ),
        "shadow raw-depth sampler",
    )
    require_compact(
        resources,
        """VK_Exec_UpdateShadowAtlasDescriptors( vkShadow.atlasSampleView,
            vkShadow.compareSampler, vkShadow.rawSampler )""",
        "atlas descriptor update requires compare and raw samplers",
    )

    point_cube = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_CreatePointCube(",
        "point shadow cube resources",
    )
    require_order(
        point_cube,
        (
            "vkShadow.compareSampler == VK_NULL_HANDLE",
            "vkShadow.rawSampler == VK_NULL_HANDLE",
            "VK_Exec_CreateShadowCubeSets( cube.cubeSampleView, vkShadow.compareSampler, vkShadow.rawSampler, cube.sets )",
        ),
        "point cube resources require compare and raw samplers",
    )

    projected_shader = read("src/renderer/Vulkan/shaders/interaction_shadow.frag")
    point_shader = read("src/renderer/Vulkan/shaders/interaction_shadow_point.frag")
    for shader, sampler_type, raw_type, context in (
        (projected_shader, "sampler2DShadow", "sampler2D", "projected shadow receiver"),
        (point_shader, "samplerCubeShadow", "samplerCube", "point shadow receiver"),
    ):
        require(
            shader,
            f"layout(set = 7, binding = 0) uniform {sampler_type} shadowCompareMap;",
            f"{context} comparison binding",
        )
        require(
            shader,
            f"layout(set = 7, binding = 2) uniform {raw_type} shadowRawMap;",
            f"{context} raw-depth binding",
        )


def validate_exact_filter_tiers(sample_body: str, sample_call: str, context: str) -> None:
    tier_start = sample_body.find("float result = " + sample_call)
    if tier_start == -1:
        raise AssertionError(f"{context} is missing the center sample that starts its tiered kernel")
    if sample_body[tier_start:].count(sample_call) != 13:
        raise AssertionError(f"{context} must issue exactly 13 samples at its maximum tier")
    if sample_body.count("rotation * vec2(") != 12:
        raise AssertionError(
            f"{context} must transform exactly the twelve off-center Poisson taps"
        )
    require_order(
        sample_body,
        (
            "float result = " + sample_call,
            "if (shadow.filterParams.y <= 1.0)",
            "return result;",
            "if (shadow.filterParams.y <= 5.0)",
            "return result * (1.0 / 5.0);",
            "if (shadow.filterParams.y <= 9.0)",
            "return result * (1.0 / 9.0);",
            "return result * (1.0 / 13.0);",
        ),
        f"{context} exact 1/5/9/13 tiers",
    )


def validate_shadow_filtering_contract() -> None:
    classification = read("src/renderer/ShadowMapClassification.cpp")
    shared_settings = braced_body(
        classification,
        "shadowMapProjectedFilterSettings_t R_ShadowMapProjectedFilterSettings(",
        "shared projected shadow filter policy",
    )
    require_order(
        shared_settings,
        (
            "const shadowMapLightClassification_t classification = R_ClassifyShadowMapLight( vLight );",
            "settings.filterScale = settings.distantSource",
            "r_shadowMapDistantFilterScale.GetFloat()",
            "settings.filterRadius = Max( 0.0f, r_shadowMapFilterRadius.GetFloat() ) * settings.filterScale;",
            "settings.filterTaps = idMath::ClampInt( 1, 13, r_shadowMapFilterTaps.GetInteger() );",
            "settings.filterMode = idMath::ClampInt( 0, 2, r_shadowMapFilterMode.GetInteger() );",
            "settings.pcssLightRadius = Max( 0.0f, r_shadowMapPCSSLightRadius.GetFloat() ) * settings.filterScale;",
            "settings.pcssMaxRadius = Max( 0.0f, r_shadowMapPCSSMaxRadius.GetFloat() ) * settings.filterScale;",
            "if ( settings.filterMode == 2 )",
            "Max( settings.pcssLightRadius, settings.pcssMaxRadius )",
        ),
        "shared projected shadow filter policy",
    )

    interactions = read("src/renderer/Vulkan/vk_Interactions.cpp")
    shadow_slice = braced_body(
        interactions,
        "static int VK_Inter_WriteShadowSlice(",
        "Vulkan shadow filter parameter upload",
    )
    require_order(
        shadow_slice,
        (
            "r_shadowMapPointFilterRadius.GetFloat()",
            "idMath::ClampInt( 1, 13, r_shadowMapPointFilterTaps.GetInteger() )",
            "idMath::ClampInt( 0, 1, r_shadowMapPointFilterMode.GetInteger() )",
            "r_shadowMapPointDepthCompare.GetBool() ? 1.0f : 0.0f",
            "R_ShadowMapProjectedFilterSettings( state->vLight )",
            "block.filterParams[ 0 ] = filterSettings.filterRadius;",
            "block.filterParams[ 1 ] = (float)filterSettings.filterTaps;",
            "block.filterParams[ 2 ] = (float)filterSettings.filterMode;",
            "r_shadowMapDepthCompare.GetBool() && filterSettings.filterMode != 2",
            "block.pcssParams[ 0 ] = filterSettings.pcssLightRadius;",
            "block.pcssParams[ 1 ] = filterSettings.pcssMaxRadius;",
            "block.pcssParams[ 2 ] = filterSettings.effectiveFilterRadius;",
            "r_shadowMapReceiverPlaneBias.GetBool() ? 1.0f : 0.0f",
        ),
        "shared projected and point runtime filter upload",
    )

    projected = read("src/renderer/Vulkan/shaders/interaction_shadow.frag")
    projected_compare = braced_body(
        projected,
        "float SampleShadowCompare(",
        "projected runtime depth comparison",
    )
    require_order(
        projected_compare,
        (
            "float compareDepth = depth - ShadowReceiverBias(cascadeIndex);",
            "if (shadow.filterParams.w > 0.5)",
            "texture(shadowCompareMap, vec3(uv, compareDepth))",
            "texture(shadowRawMap, uv).r",
            "compareDepth <= storedDepth ? 1.0 : 0.0",
        ),
        "projected runtime compare/raw selection",
    )

    projected_rotation = braced_body(
        projected,
        "mat2 ShadowOffsetRotation(",
        "projected stable Poisson rotation",
    )
    require_order(
        projected_rotation,
        (
            "if (shadow.filterParams.z < 0.5)",
            "StableShadowHash(vec3(",
            "floor(uv / max(shadow.texelSize.x, 1.0e-6))",
            "floor(depth * 1024.0)",
            "* 6.2831853",
            "return mat2(c, s, -s, c);",
        ),
        "projected stable Poisson rotation",
    )

    receiver_bias = braced_body(
        projected,
        "float ShadowReceiverBias(",
        "projected derivative receiver bias",
    )
    require_order(
        receiver_bias,
        (
            "float receiverPlaneBias = 0.0;",
            """if (shadow.pcssParams.w > 0.5
                && !ShadowDebugModeIs(kShadowDebugReceiverPlaneBiasOff))""",
            "ShadowDepthGradient(cascadeIndex)",
            "max(shadow.pcssParams.z, 1.0)",
            "max(max(texelBias, receiverPlaneBias), 0.0)",
        ),
        "projected derivative receiver bias",
    )

    blocker_search = braced_body(
        projected,
        "float ProjectedPCSSRadius(",
        "projected PCSS blocker search",
    )
    require_order(
        blocker_search,
        (
            """ShadowDebugModeIs(kShadowDebugPCFOff)
                || shadow.filterParams.z < 1.5
                || shadow.filterParams.w > 0.5""",
            "float compareDepth = depth - ShadowReceiverBias(cascadeIndex);",
            "float blockerDepth = 0.0;",
            "float blockerCount = 0.0;",
            "float d0 = RawShadowDepth(uv);",
            "float d1 = RawShadowDepth(",
            "float d2 = RawShadowDepth(",
            "float d3 = RawShadowDepth(",
            "float d4 = RawShadowDepth(",
            "float d5 = RawShadowDepth(",
            "float d6 = RawShadowDepth(",
            "float d7 = RawShadowDepth(",
            "float d8 = RawShadowDepth(",
            "if (blockerCount <= 0.0)",
            "return 0.0;",
            "float averageBlocker = blockerDepth / blockerCount;",
            "float separation = max(compareDepth - averageBlocker, 0.0);",
            "float penumbra = separation / max(averageBlocker, 1.0e-4);",
            "return clamp(max(baseRadius, penumbra * shadow.pcssParams.x), baseRadius, maxRadius);",
        ),
        "projected PCSS blocker search",
    )
    if blocker_search.count("RawShadowDepth(") != 9:
        raise AssertionError("Projected PCSS blocker search must use its fixed nine raw-depth probes")
    require(blocker_search, "mat2 rotation", "projected PCSS shared kernel rotation")
    if blocker_search.count("ShadowOffsetRotation(") != 0:
        raise AssertionError("Projected PCSS must reuse the caller's stable rotation")

    projected_samples = braced_body(
        projected,
        "float SampleShadowCascade(",
        "projected Poisson shadow filter",
    )
    validate_exact_filter_tiers(
        projected_samples,
        "SampleShadowCompare(",
        "projected Poisson shadow filter",
    )
    if projected_samples.count("ShadowOffsetRotation(") != 1:
        raise AssertionError("Projected blocker search and PCF taps must share one stable rotation")

    projected_main = braced_body(projected, "void main()", "projected shadow fragment main")
    if projected_main.count("dFdx(") != 4 or projected_main.count("dFdy(") != 4:
        raise AssertionError(
            "Projected receiver-plane bias must derive all four cascade depths before selection"
        )
    require_order(
        projected_main,
        (
            "if (shadow.pcssParams.w > 0.5)",
            "gShadowDepthGradients = vec4(",
            "abs(dFdx(vShadowCoord0.z)) + abs(dFdy(vShadowCoord0.z))",
            "abs(dFdx(vShadowCoord3.z)) + abs(dFdy(vShadowCoord3.z))",
            "light *= SampleShadowFactor();",
        ),
        "projected derivative calculation before divergent cascade sampling",
    )

    point = read("src/renderer/Vulkan/shaders/interaction_shadow_point.frag")
    point_compare = braced_body(
        point,
        "float SamplePointShadowCompare(",
        "point runtime depth comparison",
    )
    require_order(
        point_compare,
        (
            "float compareDepth = depth - ShadowReceiverBias();",
            "if (shadow.samplingParams.x > 0.5)",
            "texture(shadowCompareMap, vec4(direction, compareDepth))",
            "texture(shadowRawMap, direction).r",
            "compareDepth <= storedDepth ? 1.0 : 0.0",
        ),
        "point runtime compare/raw selection",
    )

    point_rotation = braced_body(
        point,
        "mat2 ShadowOffsetRotation(",
        "point stable tangent-disc rotation",
    )
    require_order(
        point_rotation,
        (
            "if (shadow.filterParams.z < 0.5)",
            "StableShadowHash(floor(direction * 37.0))",
            "* 6.2831853",
            "return mat2(c, s, -s, c);",
        ),
        "point stable tangent-disc rotation",
    )

    point_samples = braced_body(
        point,
        "float SampleShadowFactor()",
        "point tangent-disc shadow filter",
    )
    require_order(
        point_samples,
        (
            "vec3 direction = SafeNormalize(vPointShadowVector);",
            "vec3 tangent = SafeNormalize(cross(up, direction));",
            "vec3 bitangent = cross(direction, tangent);",
            "float tap = shadow.filterParams.w * filterRadius;",
            "tangent * o1.x + bitangent * o1.y",
        ),
        "point tangent-disc sampling basis",
    )
    validate_exact_filter_tiers(
        point_samples,
        "SamplePointShadowCompare(",
        "point tangent-disc shadow filter",
    )
    if point_samples.count("ShadowOffsetRotation(") != 1:
        raise AssertionError("Point PCF taps must share one stable rotation")

    # The shipped OpenGL 1.10 programs are manually maintained counterparts,
    # not products of the Vulkan SPIR-V header generator. Keep their blocker,
    # filter-tier, and rotation contracts pinned independently so one backend
    # cannot silently drift or compile with a different kernel.
    gl_projected = read("content/baseoq4/pak0/glprogs/shadow_interaction.fs")
    gl_blocker_search = braced_body(
        gl_projected,
        "float ProjectedPCSSRadius(",
        "OpenGL projected PCSS blocker search",
    )
    require_order(
        gl_blocker_search,
        (
            "float compareDepth = depth - ShadowReceiverBias( cascadeIndex, depth );",
            "float d0 = RawShadowDepth( uv );",
            "float d1 = RawShadowDepth(",
            "float d8 = RawShadowDepth(",
            "if ( blockerCount <= 0.0 )",
            "return 0.0;",
            "float separation = max( compareDepth - averageBlocker, 0.0 );",
        ),
        "OpenGL bias-consistent nine-probe PCSS search",
    )
    if gl_blocker_search.count("RawShadowDepth(") != 9:
        raise AssertionError("OpenGL PCSS blocker search must use exactly nine raw-depth probes")
    if gl_blocker_search.count("rotation * vec2(") != 8:
        raise AssertionError("OpenGL PCSS blocker search must use eight unique off-center probes")
    if gl_blocker_search.count("ShadowOffsetRotation(") != 0:
        raise AssertionError("OpenGL PCSS blocker search must reuse the caller's rotation")
    for tap_index in range(1, 9):
        require_compact(
            gl_blocker_search,
            f"float d{tap_index} = RawShadowDepth( clamp( uv + o{tap_index} * searchTap, clampMin, clampMax ) );",
            f"OpenGL clamped PCSS probe d{tap_index}",
        )
        require_compact(
            gl_blocker_search,
            f"if ( d{tap_index} < compareDepth ) {{ blockerDepth += d{tap_index}; blockerCount += 1.0; }}",
            f"OpenGL PCSS probe d{tap_index} contribution",
        )

    gl_projected_samples = braced_body(
        gl_projected,
        "vec4 SampleShadowCascade(",
        "OpenGL projected Poisson shadow filter",
    )
    gl_projected_tier_start = gl_projected_samples.find("float shadow = 0.0;")
    if gl_projected_tier_start == -1 or gl_projected_samples[gl_projected_tier_start:].count("SampleShadowCompare(") != 13:
        raise AssertionError("OpenGL projected PCF must issue exactly 13 samples at its maximum tier")
    if gl_projected_samples.count("rotation * vec2(") != 12:
        raise AssertionError("OpenGL projected PCF must transform twelve off-center taps")
    if gl_projected_samples.count("ShadowOffsetRotation(") != 1:
        raise AssertionError("OpenGL blocker search and PCF must share one stable rotation")
    require_order(
        gl_projected_samples,
        (
            "mat2 rotation = ShadowOffsetRotation( uv, depth );",
            "ProjectedPCSSRadius( uv, depth, cascadeIndex, clampMin, clampMax, rotation )",
            "shadow += SampleShadowCompare( uv, depth, cascadeIndex );",
            "if ( uShadowFilterTaps <= 1.0 )",
            "if ( uShadowFilterTaps <= 5.0 )",
            "if ( uShadowFilterTaps <= 9.0 )",
            "shadow * ( 1.0 / 13.0 )",
        ),
        "OpenGL exact projected filter tiers",
    )

    gl_point = read("content/baseoq4/pak0/glprogs/shadow_point_interaction.fs")
    gl_point_samples = braced_body(
        gl_point,
        "float SamplePointShadow()",
        "OpenGL point tangent-disc shadow filter",
    )
    if gl_point_samples.count("SamplePointShadowCompare(") != 14:
        # One early unfiltered return plus the complete 13-sample tiered kernel.
        raise AssertionError("OpenGL point receiver must preserve one direct and thirteen tiered samples")
    if gl_point_samples.count("rotation * vec2(") != 12:
        raise AssertionError("OpenGL point PCF must transform twelve off-center taps")
    if gl_point_samples.count("ShadowOffsetRotation(") != 1:
        raise AssertionError("OpenGL point PCF taps must share one stable rotation")
    require_order(
        gl_point_samples,
        (
            "vec3 direction = SafeNormalize( vPointShadowVector );",
            "vec3 tangent = SafeNormalize( cross( up, direction ) );",
            "vec3 bitangent = cross( direction, tangent );",
            "float tap = uPointShadowTexelScale * filterRadius;",
            "if ( uShadowFilterTaps <= 1.0 )",
            "if ( uShadowFilterTaps <= 5.0 )",
            "if ( uShadowFilterTaps <= 9.0 )",
            "shadow * ( 1.0 / 13.0 )",
        ),
        "OpenGL exact point filter tiers",
    )


def point_receiver_settings(
    far_distance: float,
    face_size: int,
    constant_bias: float,
    normal_bias: float,
    texel_bias_scale: float,
    normal_offset_scale: float,
    max_world_bias: float,
) -> tuple[float, float, float, float, float]:
    def nonnegative_finite(value: float) -> float:
        return value if math.isfinite(value) and value > 0.0 else 0.0

    far = far_distance if math.isfinite(far_distance) and far_distance > 0.0 else 1.0
    face = max(face_size, 1)
    values = [
        nonnegative_finite(constant_bias),
        nonnegative_finite(normal_bias),
        nonnegative_finite(texel_bias_scale),
        nonnegative_finite(normal_offset_scale),
    ]
    cap = nonnegative_finite(max_world_bias)
    depth_world = far * max(values[0] + values[1], values[2] / face * 5.0)
    offset_world = far * 2.0 * values[3] / face
    requested = depth_world + offset_world
    scale = cap / requested if cap > 0.0 and requested > cap else 1.0
    return (*(value * scale for value in values), scale)


def storage_adjusted_point_receiver_settings(
    base: tuple[float, float, float, float, float],
    far_distance: float,
    face_size: int,
    depth_compare: bool,
    high_precision: bool,
    max_world_bias: float,
) -> tuple[float, float, float, float, float]:
    if depth_compare:
        return base
    storage_step = 1.0 / 2048.0 if high_precision else 1.0 / 65025.0
    return point_receiver_settings(
        far_distance,
        face_size,
        max(base[0], storage_step * 1.5),
        base[1],
        base[2],
        base[3],
        max_world_bias,
    )


def validate_point_receiver_world_bias_contract() -> None:
    # Ordinary local lights retain the authored defaults.
    ordinary = point_receiver_settings(256.0, 512, 0.00010, 0.0010, 0.45, 1.0, 4.0)
    if ordinary[4] != 1.0:
        raise AssertionError("Ordinary point lights must remain below the world-bias cap")

    # Retail airdefense1 light_146: radius + abs(center), then the default 1.25 far scale.
    far_distance = math.sqrt(15488.0**2 + 16040.0**2 + 10568.0**2) * 1.25
    bounded = point_receiver_settings(
        far_distance, 512, 0.00010, 0.0010, 0.45, 1.0, 4.0
    )
    expected_scale = 0.0156235087883939
    if not math.isclose(bounded[4], expected_scale, rel_tol=1.0e-6):
        raise AssertionError(f"Unexpected airdefense1 point-bias scale: {bounded[4]}")
    bounded_world = far_distance * (
        max(bounded[0] + bounded[1], bounded[2] / 512.0 * 5.0)
        + 2.0 * bounded[3] / 512.0
    )
    if bounded_world > 4.00001:
        raise AssertionError(f"Point receiver world-bias cap exceeded: {bounded_world}")

    if storage_adjusted_point_receiver_settings(
        bounded, far_distance, 512, True, False, 4.0
    ) != bounded:
        raise AssertionError("Hardware point comparison must not apply a manual storage floor")
    for high_precision in (False, True):
        adjusted = storage_adjusted_point_receiver_settings(
            bounded, far_distance, 512, False, high_precision, 4.0
        )
        adjusted_world = far_distance * (
            max(adjusted[0] + adjusted[1], adjusted[2] / 512.0 * 5.0)
            + 2.0 * adjusted[3] / 512.0
        )
        if adjusted_world > 4.00001 or not all(math.isfinite(value) for value in adjusted):
            raise AssertionError(
                f"Manual point storage floor escaped its world cap (fp16={high_precision})"
            )

    modern_depth_only = point_receiver_settings(
        far_distance, 512, 0.00010, 0.0010, 0.45, 0.0, 4.0
    )
    if modern_depth_only[3] != 0.0:
        raise AssertionError("A receiver without geometric normal offset must reserve no offset budget")
    modern_adjusted = storage_adjusted_point_receiver_settings(
        modern_depth_only, far_distance, 512, False, False, 4.0
    )
    modern_world = far_distance * max(
        modern_adjusted[0] + modern_adjusted[1],
        modern_adjusted[2] / 512.0 * 5.0,
    )
    if modern_world > 4.00001:
        raise AssertionError("Modern manual point depth escaped the depth-only world cap")
    if point_receiver_settings(
        far_distance, 512, 0.00010, 0.0010, 0.45, 1.0, 0.0
    )[4] != 1.0:
        raise AssertionError("A zero point receiver world-bias cap must disable clamping")
    if not all(
        math.isfinite(value)
        for value in point_receiver_settings(
            math.nan, 0, math.nan, -1.0, math.inf, -math.inf, math.nan
        )
    ):
        raise AssertionError("Invalid point receiver inputs must sanitize to finite values")

    init = read("src/renderer/RenderSystem_init.cpp")
    require_compact(
        init,
        '''idCVar r_shadowMapPointMaxWorldBias( "r_shadowMapPointMaxWorldBias", "4.0",
            CVAR_RENDERER | CVAR_ARCHIVE | CVAR_FLOAT''',
        "point receiver world-bias cap default",
    )
    require(init, '0.0f, 64.0f );', "point receiver world-bias cap range")

    classification_h = read("src/renderer/ShadowMapClassification.h")
    classification = read("src/renderer/ShadowMapClassification.cpp")
    require(
        classification_h,
        "SHADOWMAP_POINT_RECEIVER_MAX_SLOPE = 4.0f",
        "CPU point receiver slope bound",
    )
    clamp = braced_body(
        classification,
        "shadowMapPointReceiverSettings_t R_ClampShadowMapPointReceiverSettings(",
        "pure point receiver world-bias clamp",
    )
    require_order(
        clamp,
        (
            "const double scalarDepthBias",
            "const double texelDepthBias",
            "SHADOWMAP_POINT_RECEIVER_MAX_SLOPE",
            "const double depthWorldBias",
            "const double normalOffsetWorldBias",
            "const double requestedWorldBias",
            "cap / requestedWorldBias",
            "settings.constantBias *= settings.worldBiasScale;",
            "settings.normalBias *= settings.worldBiasScale;",
            "settings.texelBiasScale *= settings.worldBiasScale;",
            "settings.normalOffsetScale *= settings.worldBiasScale;",
        ),
        "pure point receiver world-bias clamp",
    )

    storage_floor = braced_body(
        classification,
        "R_ShadowMapPointStorageAdjustedReceiverSettings(",
        "shared cap-bounded manual point-depth floor",
    )
    require_order(
        storage_floor,
        (
            "if ( depthCompare )",
            "const float storageStep",
            "R_ClampShadowMapPointReceiverSettings(",
            "Max( baseSettings.constantBias, storageStep * 1.5f )",
            "r_shadowMapPointMaxWorldBias.GetFloat()",
        ),
        "shared cap-bounded manual point-depth floor",
    )
    gl = read("src/renderer/draw_arb2.cpp")
    direct_gl = braced_body(
        gl,
        "static bool RB_GLSLPointShadowMap_CreateDrawInteractions(",
        "direct GL bounded point receiver upload",
    )
    require_order(
        direct_gl,
        (
            "R_ShadowMapPointFarDistance( backEnd.vLight )",
            "R_ShadowMapPointReceiverSettings(",
            "R_ShadowMapPointStorageAdjustedReceiverSettings(",
            "receiverSettings.constantBias",
            "receiverSettings.normalBias",
            "receiverSettings.texelBiasScale / pointFaceSize",
            "receiverSettings.normalOffsetScale / pointFaceSize",
        ),
        "direct GL bounded point receiver upload",
    )

    modern = read("src/renderer/ModernShadowPlanner.cpp")
    modern_contract = braced_body(
        modern,
        "static void R_ModernShadowPlanner_InitDescriptorContract(",
        "modern bounded point receiver descriptor",
    )
    require(
        modern_contract,
        "MODERN_SHADOW_COMPARE_MANUAL_PACKED_DEPTH",
        "modern OpenGL point descriptor uses manual color-depth decoding",
    )
    require_order(
        modern_contract,
        (
            "if ( descriptor.pointLight )",
            "R_ShadowMapPointFarDistance( vLight )",
            "R_ClampShadowMapPointReceiverSettings(",
            "r_shadowMapPointBias.GetFloat()",
            "r_shadowMapPointNormalBias.GetFloat()",
            "r_shadowMapTexelBiasScale.GetFloat()",
            "0.0f",
            "r_shadowMapPointMaxWorldBias.GetFloat()",
            "#ifndef OPENQ4_RENDERER_VK_MODULE",
            "R_ShadowMapPointStorageAdjustedReceiverSettings(",
            "false",
            "r_shadowMapPointHighPrecision.GetBool()",
            "#endif",
            "descriptor.bias[0] = pointReceiverSettings.constantBias;",
            "descriptor.bias[1] = pointReceiverSettings.normalBias;",
            "descriptor.texelDepthBias[0] =",
            "pointReceiverSettings.texelBiasScale",
        ),
        "modern bounded point receiver descriptor",
    )

    texture_bindings = braced_body(
        gl,
        "bool RB_ShadowMapTextureBindings(",
        "modern OpenGL manual point-depth binding",
    )
    require_order(
        texture_bindings,
        (
            "bindings.pointDepthCompare = RB_PointShadowMapDepthCompareEnabled();",
            "idImage *pointAtlasImage = g_pointShadowMapColorImage;",
            "bindings.pointAtlas",
            "pointAtlasImage",
            "RB_ShadowMapActivePointCacheContentReady()",
        ),
        "modern OpenGL manual point-depth binding independent of classic compare",
    )

    point_content_ready = braced_body(
        gl,
        "static bool RB_ShadowMapActivePointCacheContentReady(",
        "exact active point-cache content gate",
    )
    require_order(
        point_content_ready,
        (
            "g_activePointShadowMapCache",
            "entry == &g_pointShadowMapCache[i]",
            "r_shadowMapPointCacheSize.GetInteger()",
            "entry->passKind != static_cast<int>( SHADOWMAP_PASS_GLOBAL )",
            "entry->lastUpdatedFrame > tr.frameCount",
            "entry->size != RB_ShadowMapPointSizeValue()",
            "entry->colorImage != g_pointShadowMapColorImage",
            "entry->depthImage != g_pointShadowMapDepthImage",
            "entry->renderTexture != g_pointShadowMapRenderTexture",
            "entry->colorImage->IsLoaded()",
            "entry->depthImage->IsLoaded()",
        ),
        "point binding requires exact valid GLOBAL cache content",
    )
    if "? g_pointShadowMapDepthImage : g_pointShadowMapColorImage" in texture_bindings:
        raise AssertionError(
            "Modern GL point binding must not reinterpret the hardware depth cube as packed color depth"
        )
    modern_executor = read("src/renderer/ModernGLExecutor.cpp")
    modern_bind = braced_body(
        modern_executor,
        "static void R_ModernGLExecutor_BindShadowTextureSlot(",
        "modern OpenGL manual shadow sampler binding",
    )
    require(
        modern_bind,
        "glTexParameteri( target, GL_TEXTURE_COMPARE_MODE, GL_NONE );",
        "modern OpenGL point sampler disables hardware comparison",
    )
    modern_shader = read("src/renderer/ModernGLShaderLibrary.cpp")
    require_order(
        modern_shader,
        (
            "float ModernClusterDecodePointDepth",
            "uModernShadowSamplerState.z > 0.5",
            "return encodedDepth.r;",
            "encodedDepth.r + encodedDepth.g * (1.0 / 255.0)",
        ),
        "modern OpenGL fp16/packed color-depth decoding",
    )

    shadow_h = read("src/renderer/Vulkan/vk_ShadowMap.h")
    light_state = braced_body(
        shadow_h, "typedef struct vkShadowLightState_s", "Vulkan point shadow state"
    )
    require_order(
        light_state,
        ("float\t\t\t\tconstantBias;", "float\t\t\t\tnormalBias;", "float\t\t\t\ttexelDepthBias;", "float\t\t\t\tnormalOffsetWorld;"),
        "Vulkan sealed point receiver settings",
    )
    interactions = read("src/renderer/Vulkan/vk_Interactions.cpp")
    shadow_slice = braced_body(
        interactions,
        "static int VK_Inter_WriteShadowSlice(",
        "direct Vulkan bounded point receiver upload",
    )
    require_order(
        shadow_slice,
        (
            "pointBlock.biasParams[ 0 ] = state->constantBias;",
            "pointBlock.biasParams[ 1 ] = state->normalBias;",
            "pointBlock.biasParams[ 2 ] = state->texelDepthBias;",
            "pointBlock.biasParams[ 3 ] = state->normalOffsetWorld;",
        ),
        "direct Vulkan bounded point receiver upload",
    )

    vk = read("src/renderer/Vulkan/vk_ShadowMap.cpp")
    vk_prepare = braced_body(
        vk,
        "int VK_ShadowMap_PrepareViewLights(",
        "Vulkan sealed point receiver policy",
    )
    require_order(
        vk_prepare,
        (
            "R_ShadowMapPointReceiverSettings(",
            "entry.constantBias = receiverSettings.constantBias;",
            "entry.normalBias = receiverSettings.normalBias;",
            "entry.texelDepthBias = receiverSettings.texelBiasScale",
            "entry.normalOffsetWorld =",
            "receiverSettings.normalOffsetScale",
        ),
        "Vulkan sealed point receiver policy",
    )

    gl_point_vertex = read(
        "content/baseoq4/pak0/glprogs/shadow_point_interaction.vs"
    )
    require_order(
        gl_point_vertex,
        (
            "vec3 pointShadowVector = worldPos - uGlobalLightOrigin.xyz;",
            "float pointShadowSinTheta",
            "uPointShadowNormalOffsetWorld * length( pointShadowVector )",
            "vPointShadowVector = pointShadowVector + worldNormal * pointShadowNormalOffset;",
        ),
        "OpenGL bounded point normal-offset consumption",
    )
    vk_point_vertex = read(
        "src/renderer/Vulkan/shaders/interaction_shadow_point.vert"
    )
    require_order(
        vk_point_vertex,
        (
            "vec3 pointShadowVector = worldPos - shadow.lightOriginFar.xyz;",
            "float shadowSinTheta",
            "shadow.biasParams.w * length(pointShadowVector)",
            "vPointShadowVector = pointShadowVector + worldNormal * normalOffset;",
        ),
        "Vulkan bounded point normal-offset consumption",
    )


def validate_shadow_contact_and_gl_robustness_contract() -> None:
    init = read("src/renderer/RenderSystem_init.cpp")
    common = read("src/framework/Common.cpp")
    interaction = read("src/renderer/Interaction.cpp")
    domain_h = read("src/renderer/ClassicInteractionDomain.h")
    domain = read("src/renderer/ClassicInteractionDomain.cpp")
    arb2_parity_h = read("src/renderer/ShadowMapArb2Parity.h")
    gl = read("src/renderer/draw_arb2.cpp")
    modern = read("src/renderer/ModernShadowPlanner.cpp")
    modern_h = read("src/renderer/ModernShadowPlanner.h")
    modern_executor = read("src/renderer/ModernGLExecutor.cpp")
    modern_shader = read("src/renderer/ModernGLShaderLibrary.cpp")
    tr_local = read("src/renderer/tr_local.h")
    vk = read("src/renderer/Vulkan/vk_ShadowMap.cpp")

    init_cvars = braced_body(init, "void R_InitCvars( void )", "renderer cvar registration")
    if "r_shadowMapContactQualityMigrated" in init_cvars:
        raise AssertionError(
            "Shadow quality migration must not run before archived configs are executed"
        )
    migration = braced_body(
        init,
        "static void R_MigrateLegacyShadowMapContactQuality( void )",
        "post-config shadow quality migration",
    )
    require_order(
        migration,
        (
            "!r_shadowMapContactQualityMigrated.GetBool()",
            "r_shadowMapFilterRadius.GetFloat() - 2.0f",
            "r_shadowMapPointFilterRadius.GetFloat() - 2.5f",
            "r_shadowMapFilterTaps.GetInteger() == 13",
            "r_shadowMapPointFilterTaps.GetInteger() == 13",
            "r_shadowMapPolygonFactor.GetFloat() - 0.75f",
            "r_shadowMapFilterRadius.SetFloat( 0.75f )",
            "r_shadowMapPointFilterRadius.SetFloat( 1.0f )",
            "r_shadowMapFilterTaps.SetInteger( 9 )",
            "r_shadowMapPointFilterTaps.SetInteger( 9 )",
            "r_shadowMapPolygonFactor.SetFloat( 0.25f )",
            "r_shadowMapContactQualityMigrated.SetBool( true )",
        ),
        "complete legacy shadow tuple migration",
    )

    legacy_profile = (2.0, 2.5, 13, 13, 0.75)
    balanced_profile = (0.75, 1.0, 9, 9, 0.25)

    def migrate_profile(
        profile: tuple[float, float, int, int, float], migrated: bool
    ) -> tuple[tuple[float, float, int, int, float], bool]:
        if migrated:
            return profile, True
        return (balanced_profile if profile == legacy_profile else profile), True

    if migrate_profile(legacy_profile, False) != (balanced_profile, True):
        raise AssertionError("The exact legacy shadow tuple must migrate once")
    custom_profile = (1.5, 2.5, 13, 13, 0.75)
    if migrate_profile(custom_profile, False) != (custom_profile, True):
        raise AssertionError("Customized shadow profiles must be preserved")
    if migrate_profile(legacy_profile, True) != (legacy_profile, True):
        raise AssertionError("The shadow contact migration flag must make migration one-shot")
    for compiled_default in (
        'r_shadowMapFilterRadius( "r_shadowMapFilterRadius", "0.75"',
        'r_shadowMapPointFilterRadius( "r_shadowMapPointFilterRadius", "1.0"',
        'r_shadowMapFilterTaps( "r_shadowMapFilterTaps", "9"',
        'r_shadowMapPointFilterTaps( "r_shadowMapPointFilterTaps", "9"',
        'r_shadowMapPolygonFactor( "r_shadowMapPolygonFactor", "0.25"',
    ):
        require(init, compiled_default, "compiled balanced contact-shadow defaults")
    renderer_device_init = braced_body(
        init,
        "void idRenderSystemLocal::InitOpenGL( void )",
        "post-config renderer device startup",
    )
    require_order(
        renderer_device_init,
        (
            "R_MigrateLegacyShadowMapContactQuality();",
            "if ( !glConfig.isInitialized )",
        ),
        "migration before either renderer device starts",
    )
    common_init = braced_body(common, "void idCommonLocal::InitGame( void )", "engine startup")
    require_order(
        common_init,
        (
            "renderSystem->Init();",
            '"exec " CONFIG_FILE "\\n"',
            "cmdSystem->ExecuteCommandBuffer();",
            "StartupVariable( NULL, false );",
            "InitRenderSystem();",
        ),
        "archived config and command-line ordering before renderer device startup",
    )
    common_outer_init = braced_body(
        common,
        "void idCommonLocal::Init( int argc, const char **argv, const char *cmdline )",
        "outer engine startup",
    )
    require_order(
        common_outer_init,
        (
            "InitGame();",
            "AddStartupCommands()",
        ),
        "late explicit +set commands remain authoritative after archived migration",
    )

    require(domain_h, "bool\t\t\tperfectHull;", "sealed caster topology")
    require(domain, "HashBool( hash, caster.perfectHull );", "caster topology identity")
    for token in (
        "caster.perfectHull = drawSurf->geo->perfectHull;",
        "caster.perfectHull = casterGeometry->perfectHull;",
    ):
        require(domain, token, "actual caster topology capture")

    caster_signature = braced_body(
        interaction,
        "static void R_RecordShadowMapCaster(",
        "physical shadow caster cache signature",
    )
    require_order(
        caster_signature,
        (
            "shader->GetCullType()",
            "casterTris->perfectHull",
            "casterTris->numVerts",
            "casterTris->numIndexes",
            "casterTris->bounds[ corner ][ component ]",
            "entityDef->lastModifiedFrameNum",
            "entityDef->dynamicModelFrameCount",
            "entityDef->modelMatrix[i]",
        ),
        "topology/material/transform-aware physical caster signature",
    )
    if interaction.count("casterTris, false, shadowMapCasterOnly") != 1 or interaction.count(
        "casterTris, true, shadowMapCasterOnly"
    ) != 1:
        raise AssertionError("Both opaque and translucent caster records must hash actual geometry")

    classification_h = read("src/renderer/ShadowMapClassification.h")
    classification = read("src/renderer/ShadowMapClassification.cpp")
    require(
        classification_h,
        "R_ShadowMapLightOriginInsideCasterBounds(",
        "shared enclosing-light caster guard declaration",
    )
    require(
        classification_h,
        "R_ShadowMapCasterTransformNeedsTwoSided(",
        "shared unsafe-transform caster guard declaration",
    )
    transform_guard = braced_body(
        classification,
        "bool R_ShadowMapCasterTransformNeedsTwoSided(",
        "shared unsafe-transform caster guard",
    )
    require_order(
        transform_guard,
        (
            "if ( modelMatrix == NULL )",
            "return true;",
            "const double determinant =",
            "static_cast<double>( modelMatrix[ 0 ] )",
            "return !std::isfinite( determinant ) || determinant <= 0.0;",
        ),
        "fail-conservative mirrored/non-finite transform guard",
    )
    enclosing_guard = braced_body(
        classification,
        "bool R_ShadowMapLightOriginInsideCasterBounds(",
        "shared enclosing-light caster guard",
    )
    require_order(
        enclosing_guard,
        (
            "R_GlobalPointToLocal(",
            "minimum > maximum",
            "return true;",
            "local < minimum || local > maximum",
            "return false;",
        ),
        "fail-conservative enclosing-light bounds guard",
    )

    def expected_cull(
        mode: int,
        material: str,
        perfect_hull: bool | None,
        light_inside_bounds: bool,
        unsafe_transform: bool = False,
    ) -> str:
        if mode == 0 or material == "two":
            return "none"
        if mode == 2 and (
            perfect_hull is not True or unsafe_transform or light_inside_bounds
        ):
            return "none"
        return "back" if material == "back" else "front"

    for material in ("front", "back", "two"):
        if expected_cull(0, material, True, False) != "none":
            raise AssertionError("Caster culling mode 0 must be unconditionally two-sided")
    if expected_cull(1, "front", False, True) != "front":
        raise AssertionError("Caster culling mode 1 must force the near-shell orientation")
    if expected_cull(1, "back", True, False) != "back":
        raise AssertionError("Back-sided material orientation must reverse forced near-shell culling")
    for topology, inside in ((False, False), (None, False), (True, True)):
        if expected_cull(2, "front", topology, inside) != "none":
            raise AssertionError("AUTO culling must fail conservatively to two-sided depth")
    if expected_cull(2, "front", True, False) != "front":
        raise AssertionError("AUTO may cull only a sealed hull whose bounds exclude the light")
    if expected_cull(2, "front", True, False, True) != "none":
        raise AssertionError("AUTO must render mirrored/non-finite transforms two-sided")
    if expected_cull(1, "front", True, False, True) != "front":
        raise AssertionError("Explicit caster culling mode 1 must retain force semantics")

    gl_cull = braced_body(
        gl,
        "static void RB_ShadowMapApplyCasterCull(",
        "direct OpenGL topology-aware caster culling",
    )
    require_order(
        gl_cull,
        (
            "mode == 2",
            "casterGeo == NULL || !casterGeo->perfectHull",
            "R_ShadowMapCasterTransformNeedsTwoSided( modelMatrix )",
            "R_ShadowMapLightOriginInsideCasterBounds( backEnd.vLight",
            "mode != 0 && !automaticTwoSided",
            "GLenum cullFace = GL_FRONT;",
            "materialCull == CT_BACK_SIDED",
        ),
        "direct OpenGL near-shell/uncertain-hull policy",
    )
    gl_shared_cull = braced_body(
        gl,
        "static void RB_SharedWorldInteractionGLApplyMapCasterCull(",
        "shared OpenGL topology-aware caster culling",
    )
    require_order(
        gl_shared_cull,
        (
            "pass.casterCullMode == 2",
            "!caster.perfectHull",
            "R_ShadowMapLightOriginInsideCasterBounds(",
            "pass.legacyViewLight",
            "pass.casterCullMode != 0 && !automaticTwoSided",
            "GLenum face = GL_FRONT;",
        ),
        "shared OpenGL near-shell/uncertain-hull policy",
    )
    vk_cull = braced_body(
        vk,
        "static VkCullModeFlags VK_ShadowMap_CasterCullMode(",
        "direct Vulkan topology-aware caster culling",
    )
    require_order(
        vk_cull,
        (
            "mode == 0 || materialCull == CT_TWO_SIDED",
            "casterGeo == NULL || !casterGeo->perfectHull",
            "surf == NULL || surf->space == NULL",
            "R_ShadowMapCasterTransformNeedsTwoSided(",
            "surf->space->modelMatrix",
            "R_ShadowMapLightOriginInsideCasterBounds( vLight",
            "bool cullFront = true;",
        ),
        "direct Vulkan near-shell/uncertain-hull policy",
    )
    vk_shared_cull = braced_body(
        vk,
        "static VkCullModeFlags VK_ClassicShadow_EffectiveCull(",
        "shared Vulkan topology-aware caster culling",
    )
    require_order(
        vk_shared_cull,
        (
            "pass.casterCullMode == 0",
            "pass.casterCullMode == 2",
            "!casterPlan.caster->perfectHull",
            "R_ShadowMapLightOriginInsideCasterBounds(",
            "pass.legacyViewLight",
            "bool cullFront = true;",
        ),
        "shared Vulkan near-shell/uncertain-hull policy",
    )
    for forbidden in ("mode == 1 ) ? GL_FRONT : GL_BACK", "mode 2 (default) stores engine-back"):
        if forbidden in gl or forbidden in vk:
            raise AssertionError(f"Far-shell caster policy survived: {forbidden!r}")

    gl_signature = braced_body(
        gl,
        "static int RB_ShadowMapBuildPassSignatureForView(",
        "OpenGL world-scoped cache signature",
    )
    for token in (
        "renderWorld->mapFileCRC",
        "RB_ShadowMapMapNameHash( viewDef )",
        "vLight->shadowMapIncompleteMapMask",
        "vLight->shadowMapHybridIncompleteMask",
        "vLight->shadowMapPrelightMapMissingMask",
        "R_ShadowMapPointFarDistance( vLight )",
        "vLight->lightDef->parms.lightCenter[i]",
    ):
        require(gl_signature, token, "OpenGL world-scoped cache signature")

    for token in (
        "unsigned int\t\t\t\t\tcacheKeyHitMask;",
        "unsigned int\t\t\t\t\tplannedCacheUpdateKeyMask;",
    ):
        require(arb2_parity_h, token, "canonical ARB2 cache estimate ABI")
    cache_estimate = braced_body(
        gl,
        "static void RB_ShadowMapEstimateArb2CachePass(",
        "canonical ARB2 cache estimate",
    )
    require_order(
        cache_estimate,
        (
            "const unsigned int passMask",
            "RB_ShadowMapCachePassKind( vLight, passKind )",
            "const unsigned int cacheKeyMask",
            "RB_ShadowMapStaticCacheableReadOnly( vLight, cachePassKind",
            "RB_ShadowMapBuildPassSignatureForView( vLight, viewDef, cachePassKind",
            "estimate.cacheKeyHitMask |= cacheKeyMask;",
            "estimate.plannedCacheUpdateKeyMask & cacheKeyMask",
            "estimate.cacheHitPassMask |= passMask;",
            "estimate.freshUpdatePasses++;",
            "estimate.plannedCacheUpdateKeyMask |= cacheKeyMask;",
        ),
        "requested receiver masks remain separate from canonical cache keys",
    )
    cache_admission = braced_body(
        gl,
        "static void RB_ShadowMapBuildUpdateAdmissions(",
        "canonical ARB2 update admission cost",
    )
    require_compact(
        cache_admission,
        """const int cost = estimate.freshUpdatePasses
            + estimate.budgetFallbackPasses;""",
        "simplified canonical update admission cost",
    )
    for obsolete_cost in (
        "collapsedFreshUpdatePasses",
        "estimate.freshUpdatePassMask & SHADOWMAP_ARB2_CACHE_PASS_GLOBAL",
    ):
        if obsolete_cost in cache_admission:
            raise AssertionError(
                f"Obsolete LOCAL/GLOBAL admission collapse survived: {obsolete_cost}"
            )
    gl_cache_view = braced_body(
        gl,
        "bool RB_ShadowMapPrepareCacheView(",
        "OpenGL map transition invalidation",
    )
    require_order(
        gl_cache_view,
        (
            "renderWorld->mapFileCRC",
            "RB_ShadowMapMapNameHash( viewDef )",
            "g_projectedShadowMapCache[i].valid = false;",
            "g_pointShadowMapCache[i].valid = false;",
            "memset( g_shadowMapLightHistory, 0",
            "g_activeProjectedShadowMapCache = NULL;",
            "g_activePointShadowMapCache = NULL;",
            "g_shadowMapDepthImage = NULL;",
            "g_pointShadowMapColorImage = NULL;",
            "memset( &g_projectedShadowMapState, 0",
            "g_projectedTranslucentShadowPassReady = false;",
            "g_pointTranslucentShadowPassReady = false;",
            "g_shadowMapCacheRenderWorld = renderWorld;",
        ),
        "OpenGL map transition invalidates cache, aliases, and moment readiness",
    )

    projected_scratch = braced_body(
        gl,
        "static void RB_ShadowMapSelectProjectedScratchResources(",
        "projected-only scratch selection",
    )
    point_scratch = braced_body(
        gl,
        "static void RB_ShadowMapSelectPointScratchResources(",
        "point-only scratch selection",
    )
    for token in (
        "g_activeProjectedShadowMapCache = NULL;",
        "g_shadowMapDepthImage = g_shadowMapScratchDepthImage;",
        "g_shadowMapRenderTexture = g_shadowMapScratchRenderTexture;",
    ):
        require(projected_scratch, token, "projected-only scratch selection")
    for forbidden in ("g_activePointShadowMapCache", "g_pointShadowMap"):
        if forbidden in projected_scratch:
            raise AssertionError(
                f"Projected scratch selection invalidates point provenance: {forbidden}"
            )
    for token in (
        "g_activePointShadowMapCache = NULL;",
        "g_pointShadowMapColorImage = g_pointShadowMapScratchColorImage;",
        "g_pointShadowMapDepthImage = g_pointShadowMapScratchDepthImage;",
        "g_pointShadowMapRenderTexture = g_pointShadowMapScratchRenderTexture;",
    ):
        require(point_scratch, token, "point-only scratch selection")
    for forbidden in ("g_activeProjectedShadowMapCache", "g_shadowMapDepthImage"):
        if forbidden in point_scratch:
            raise AssertionError(
                f"Point scratch selection invalidates projected provenance: {forbidden}"
            )
    direct_schedule = braced_body(
        gl,
        "static shadowMapSchedule_t RB_ShadowMapSchedulePass(",
        "type-specific direct scratch selection",
    )
    require_order(
        direct_schedule,
        (
            "if ( pointLight )",
            "RB_ShadowMapSelectPointScratchResources();",
            "else",
            "RB_ShadowMapSelectProjectedScratchResources();",
            "schedule.cacheable =",
        ),
        "point and projected scratch aliases are selected independently",
    )

    point_storage = braced_body(
        gl,
        "static bool RB_ShadowMapPointCacheEntryStorageValid(",
        "point-cube physical storage validation",
    )
    require_order(
        point_storage,
        (
            "entry->colorStorageGeneration != 0",
            "entry->depthStorageGeneration != 0",
            "entry->colorImage->GetStorageGeneration()",
            "entry->depthImage->GetStorageGeneration()",
            "entry->colorImage->IsLoaded()",
            "!entry->colorImage->IsDefaulted()",
            "entry->colorImage->GetDeviceHandle() != 0",
            "entry->depthImage->IsLoaded()",
            "!entry->depthImage->IsDefaulted()",
            "entry->depthImage->GetDeviceHandle() != 0",
            "entry->colorImage->GetOpts().textureType == TT_CUBIC",
            "entry->depthImage->GetOpts().textureType == TT_CUBIC",
            "entry->colorImage->GetOpts().format",
            "entry->highPrecision ? FMT_RGBA16F : FMT_RGBA8",
            "entry->depthImage->GetOpts().format == FMT_DEPTH",
            "entry->renderTexture->GetWidth() == entry->size",
            "entry->renderTexture->GetHeight() == entry->size",
        ),
        "point-cube storage identity is generation/type/format/dimension exact",
    )
    for field in ("lightOrigin[3];", "farDistance;"):
        require(gl, field, "point-cube render-time projection provenance")
    point_active = braced_body(
        gl,
        "static bool RB_ShadowMapActivePointCacheContentReady(",
        "active point-cube provenance validation",
    )
    require_order(
        point_active,
        (
            "entry->farDistance > 0.0f",
            "entry->lightOrigin[0]",
            "entry->colorStorageGeneration == 0",
            "entry->depthStorageGeneration == 0",
            "entry->colorImage->GetStorageGeneration()",
            "entry->depthImage->GetStorageGeneration()",
        ),
        "active point cubes require published projection and storage provenance",
    )
    point_projection = braced_body(
        gl,
        "static bool RB_ShadowMapPointCacheEntryProjectionMatches(",
        "point-cube render-time projection identity",
    )
    require_order(
        point_projection,
        (
            "entry->farDistance == R_ShadowMapPointFarDistance( vLight )",
            "entry->lightOrigin[0] == vLight->globalLightOrigin[0]",
            "entry->lightOrigin[1] == vLight->globalLightOrigin[1]",
            "entry->lightOrigin[2] == vLight->globalLightOrigin[2]",
        ),
        "compatible point history cannot cross light-origin or far-plane changes",
    )
    point_history = braced_body(
        gl,
        "static pointShadowMapCacheEntry_t *RB_ShadowMapNewestCompatiblePointEntry(",
        "point-cube admission history compatibility",
    )
    require_order(
        point_history,
        (
            "requiredSize = RB_ShadowMapPointSizeValue()",
            "requiredHighPrecision = RB_PointShadowMapHighPrecisionEnabled()",
            "requiredDepthCompare = RB_PointShadowMapDepthCompareEnabled()",
            "RB_ShadowMapPointCacheEntryStorageValid( entry )",
            "entry->size == requiredSize",
            "entry->highPrecision == requiredHighPrecision",
            "entry->depthCompare == requiredDepthCompare",
            "RB_ShadowMapPointCacheEntryProjectionMatches( entry, vLight )",
            "entry->lastUpdatedFrame > newest->lastUpdatedFrame",
            "newest = entry;",
            "return newest;",
        ),
        "point-cube history selects the newest projection-compatible allocation",
    )
    projected_storage = braced_body(
        gl,
        "static bool RB_ShadowMapProjectedCacheEntryStorageValid(",
        "projected-atlas physical storage validation",
    )
    require_order(
        projected_storage,
        (
            "entry->atlasStorageGeneration != 0",
            "g_shadowMapAtlasDepthImage != NULL",
            "g_shadowMapAtlasRenderTexture != NULL",
            "g_shadowMapAtlasDepthImage->GetStorageGeneration()",
            "entry->atlasStorageGeneration",
            "g_shadowMapAtlasDepthImage->IsLoaded()",
            "!g_shadowMapAtlasDepthImage->IsDefaulted()",
            "g_shadowMapAtlasDepthImage->GetDeviceHandle() != 0",
            "g_shadowMapAtlasDepthImage->GetOpts().textureType == TT_2D",
            "g_shadowMapAtlasDepthImage->GetOpts().format == FMT_DEPTH",
            "g_shadowMapAtlasRenderTexture->GetWidth() > 0",
            "g_shadowMapAtlasRenderTexture->GetHeight() > 0",
        ),
        "projected cache entries require the atlas allocation they rendered into",
    )
    for lookup_name in (
        "static projectedShadowMapCacheEntry_t *RB_ShadowMapFindProjectedCacheEntry(",
        "static projectedShadowMapCacheEntry_t *RB_ShadowMapNewestProjectedGlobalEntry( const int lightIndex ) {",
    ):
        require(
            braced_body(gl, lookup_name, "projected cache lookup generation gate"),
            "RB_ShadowMapProjectedCacheEntryStorageValid( entry )",
            "projected cache lookups reject stale atlas allocations",
        )
    direct_cache_completion = braced_body(
        gl,
        "static void RB_ShadowMapCompleteCacheUpdate(",
        "direct projected cache storage publication",
    )
    require_order(
        direct_cache_completion,
        (
            "entry->lastUpdatedFrame = tr.frameCount;",
            "entry->atlasStorageGeneration = g_shadowMapAtlasDepthImage != NULL",
            "g_shadowMapAtlasDepthImage->GetStorageGeneration()",
            "entry->state = g_projectedShadowMapState;",
        ),
        "direct projected cache publication stamps the rendered atlas allocation",
    )
    require_order(
        direct_cache_completion,
        (
            "entry->lightOrigin[0] = vLight->globalLightOrigin[0];",
            "entry->lightOrigin[1] = vLight->globalLightOrigin[1];",
            "entry->lightOrigin[2] = vLight->globalLightOrigin[2];",
            "entry->farDistance = R_ShadowMapPointFarDistance( vLight );",
            "entry->lastUpdatedFrame = tr.frameCount;",
            "entry->colorStorageGeneration = entry->colorImage != NULL",
            "entry->depthStorageGeneration = entry->depthImage != NULL",
        ),
        "direct point-cache publication stamps projection and storage provenance",
    )
    shared_cache_completion = braced_body(
        gl,
        "static void RB_SharedWorldInteractionGLCompleteSealedCacheUpdate(",
        "shared projected cache storage publication",
    )
    require_order(
        shared_cache_completion,
        (
            "entry->lastUpdatedFrame = tr.frameCount;",
            "entry->atlasStorageGeneration = preparedPass.projectedDepthImage != NULL",
            "preparedPass.projectedDepthImage->GetStorageGeneration()",
            "entry->state = pass.projected.state;",
        ),
        "shared projected cache publication stamps the rendered atlas allocation",
    )
    require_order(
        shared_cache_completion,
        (
            "entry->lightOrigin[0] = pass.point.lightOrigin[0];",
            "entry->lightOrigin[1] = pass.point.lightOrigin[1];",
            "entry->lightOrigin[2] = pass.point.lightOrigin[2];",
            "entry->farDistance = pass.point.farDistance;",
            "entry->lastUpdatedFrame = tr.frameCount;",
            "entry->colorStorageGeneration = entry->colorImage != NULL",
            "entry->depthStorageGeneration = entry->depthImage != NULL",
        ),
        "shared point-cache publication stamps sealed projection and storage provenance",
    )
    shared_projected_match = braced_body(
        gl,
        "static bool RB_SharedWorldInteractionGLProjectedEntryMatches(",
        "shared projected cache generation revalidation",
    )
    require_order(
        shared_projected_match,
        (
            "entry->atlasStorageGeneration != 0",
            "entry->atlasStorageGeneration",
            "preparedPass.sampleStorageGeneration",
        ),
        "shared projected consumption preserves cache-to-storage provenance",
    )
    shared_point_match = braced_body(
        gl,
        "static bool RB_SharedWorldInteractionGLPointEntryMatches(",
        "shared point-cache projection revalidation",
    )
    require_order(
        shared_point_match,
        (
            "entry->lightOrigin[0] == pass.point.lightOrigin[0]",
            "entry->lightOrigin[1] == pass.point.lightOrigin[1]",
            "entry->lightOrigin[2] == pass.point.lightOrigin[2]",
            "entry->farDistance == pass.point.farDistance",
            "entry->colorStorageGeneration != 0",
            "entry->depthStorageGeneration != 0",
        ),
        "shared point-cache consumption preserves projection and storage provenance",
    )
    point_bindings = braced_body(
        gl,
        "bool RB_ShadowMapTextureBindings(",
        "point-cube bind-time identity export",
    )
    require_order(
        point_bindings,
        (
            "bindings.pointAtlasLightIndex = -1;",
            "bindings.pointAtlasContentFrame = -1;",
            "RB_ShadowMapActivePointCacheContentReady()",
            "bindings.pointAtlasLightIndex = g_activePointShadowMapCache->lightIndex;",
            "bindings.pointAtlasSignature = g_activePointShadowMapCache->signature;",
            "bindings.pointAtlasContentFrame = g_activePointShadowMapCache->lastUpdatedFrame;",
        ),
        "point sampler binding carries exact cache provenance",
    )
    for field in (
        "pointAtlasLightIndex;",
        "pointAtlasSignature;",
        "pointAtlasContentFrame;",
    ):
        require(tr_local, field, "renderer point-cube binding ABI")

    for declaration in (
        "bool RB_ShadowMapProjectedAtlasSlotForLight( const viewLight_t *vLight,",
        "const viewDef_t *viewDef, shadowMapArb2AtlasSlot_t &slot );",
        "bool RB_ShadowMapProjectedAtlasSlotMarkUsed( int lightDefIndex,",
        "int signature, std::uint64_t storageGeneration,",
        "int cellX, int cellY, int cellSpan );",
    ):
        require(arb2_parity_h, declaration, "exact projected-atlas provenance API")
    require(arb2_parity_h, "storageGeneration;", "projected-atlas slot generation ABI")
    projected_slot_export = braced_body(
        gl,
        "bool RB_ShadowMapProjectedAtlasSlotForLight(",
        "exact projected-atlas provenance export",
    )
    require_order(
        projected_slot_export,
        (
            "viewDef->renderWorld == NULL",
            "g_shadowMapCacheRenderWorld != viewDef->renderWorld",
            "g_shadowMapCacheMapFileCRC != viewDef->renderWorld->mapFileCRC",
            "g_shadowMapCacheMapNameHash != RB_ShadowMapMapNameHash( viewDef )",
            "g_shadowMapAtlasDepthImage->IsDefaulted()",
            "g_shadowMapAtlasDepthImage->GetOpts().textureType != TT_2D",
            "g_shadowMapAtlasDepthImage->GetOpts().format != FMT_DEPTH",
            "RB_ShadowMapBuildPassSignatureForView(",
            "SHADOWMAP_PASS_GLOBAL, false",
            "RB_ShadowMapFindProjectedCacheEntry(",
            "lightDefIndex, SHADOWMAP_PASS_GLOBAL, signature",
            "slot.signature = entry->signature;",
            "slot.storageGeneration = entry->atlasStorageGeneration;",
            "slot.lastUpdatedFrame = entry->lastUpdatedFrame;",
            "slot.valid = true;",
        ),
        "projected slot export requires the exact current GLOBAL signature",
    )
    if "RB_ShadowMapNewestProjectedGlobalEntry(" in projected_slot_export:
        raise AssertionError(
            "Projected provenance export must not select a merely newest stale sibling"
        )
    projected_slot_pin = braced_body(
        gl,
        "bool RB_ShadowMapProjectedAtlasSlotMarkUsed(",
        "exact projected-atlas residency pin",
    )
    require_order(
        projected_slot_pin,
        (
            "RB_ShadowMapFindProjectedCacheEntry(",
            "lightDefIndex, SHADOWMAP_PASS_GLOBAL, signature",
            "entry->atlasStorageGeneration != storageGeneration",
            "entry->atlasCellX != cellX",
            "entry->atlasCellY != cellY",
            "entry->atlasCellSpan != cellSpan",
            "return false;",
            "entry->lastUsedFrame = tr.frameCount;",
            "return true;",
        ),
        "projected slot pin revalidates signature, storage generation, and physical cell identity",
    )

    point_cube_export = braced_body(
        gl,
        "bool RB_ShadowMapPointCubeForLight(",
        "exact point-cube provenance export",
    )
    require_order(
        point_cube_export,
        (
            "viewDef->renderWorld == NULL",
            "g_shadowMapCacheRenderWorld != viewDef->renderWorld",
            "g_shadowMapCacheMapFileCRC != viewDef->renderWorld->mapFileCRC",
            "g_shadowMapCacheMapNameHash != RB_ShadowMapMapNameHash( viewDef )",
            "!RB_ShadowMapActivePointCacheContentReady()",
            "RB_ShadowMapBuildPassSignatureForView(",
            "SHADOWMAP_PASS_GLOBAL",
            "entry->lightIndex != cube.lightIndex",
            "entry->signature != signature",
            "cube.lastUpdatedFrame = entry->lastUpdatedFrame;",
            "return true;",
        ),
        "point cube export is scoped to exact world, light, pass, and signature",
    )

    point_cube_mark_used = braced_body(
        gl,
        "void RB_ShadowMapPointCubeMarkUsed(",
        "exact point-cube residency pin",
    )
    require_order(
        point_cube_mark_used,
        (
            "RB_ShadowMapActivePointCacheContentReady()",
            "g_activePointShadowMapCache->lightIndex == lightDefIndex",
            "lastUsedFrame = tr.frameCount;",
        ),
        "only the selected valid point cube can be pinned",
    )

    hybrid_available = braced_body(
        gl,
        "static bool RB_ShadowMapHybridAvailableForPass( const viewLight_t *vLight,\n\t\tconst shadowMapPassKind_t passKind )",
        "direct ARB2 hybrid availability",
    )
    require_order(
        hybrid_available,
        (
            "RB_ShadowMapReceiverMaskForPass( passKind )",
            "vLight->shadowMapIncompleteMapMask",
            "vLight->shadowMapPrelightMapMissingMask",
            "incompleteMapMask & receiverMask",
            "vLight->shadowMapPrelightMapMissingMask & receiverMask",
            "vLight->shadowMapHybridIncompleteMask & receiverMask",
            "vLight->globalShadowMapStencilSupplements != NULL",
            "passKind == SHADOWMAP_PASS_GLOBAL",
            "vLight->localShadowMapStencilSupplements != NULL",
        ),
        "direct hybrid accepts only exact ordinary supplement ownership",
    )
    hybrid_stencil = braced_body(
        gl,
        "static bool RB_ShadowMapPrepareMappedReceiverStencil(",
        "direct ARB2 mapped supplement stencil",
    )
    require_order(
        hybrid_stencil,
        (
            "if ( !hybrid )",
            "glStencilFunc( GL_ALWAYS, 128, 255 );",
            "vLight->globalShadowMapStencilSupplements",
            "passKind == SHADOWMAP_PASS_GLOBAL",
            "vLight->localShadowMapStencilSupplements",
            "globalSupplements == NULL && localSupplements == NULL",
            "glScissor(",
            "glClear( GL_STENCIL_BUFFER_BIT );",
            "VPROG_STENCIL_SHADOW",
            "RB_StencilShadowPass( globalSupplements );",
            "RB_StencilShadowPass( localSupplements );",
            "glStencilFunc( GL_GEQUAL, 128, 255 );",
        ),
        "direct hybrid clears and stamps only the missing caster supplements",
    )
    direct_shadow_pass = braced_body(
        gl,
        "static void RB_ShadowMapRunPass(",
        "direct ARB2 incomplete-map failover",
    )
    map_or_hybrid = braced_body(
        gl,
        "static bool RB_ShadowMapMapOrHybridAvailableForPass(\n\t\tconst viewLight_t *vLight",
        "shared direct/estimator completeness predicate",
    )
    require_order(
        map_or_hybrid,
        (
            "RB_ShadowMapReceiverMaskForPass( passKind )",
            "vLight->shadowMapIncompleteMapMask",
            "vLight->shadowMapPrelightMapMissingMask",
            "if ( !mapIncomplete )",
            "RB_ShadowMapHybridAvailableForPass(",
            "*hybridOut = hybrid;",
            "return hybrid;",
        ),
        "complete maps or exact hybrids share one fail-closed predicate",
    )
    require_order(
        direct_shadow_pass,
        (
            "RB_ShadowMapMapOrHybridAvailableForPass(",
            "vLight, passKind, &hybrid",
            "passKind == SHADOWMAP_PASS_GLOBAL",
            "g_shadowMapGlobalPassHybrid = hybrid;",
            "if ( !mapOrHybridAvailable )",
            "RB_ShadowMapMarkStencilFallbackSticky( vLight );",
            "if ( !deferReceiverDraw )",
            "RB_ShadowMapSchedulePass(",
        ),
        "incomplete direct maps fail closed before scheduling unless hybrid-complete",
    )
    if direct_shadow_pass.count("RB_ShadowMapPrepareMappedReceiverStencil(") != 2:
        raise AssertionError(
            "Direct ARB2 must stamp hybrid supplements on both cache reuse and fresh map paths"
        )
    reuse_path = braced_body(
        direct_shadow_pass,
        "if ( schedule.action == SHADOWMAP_SCHEDULE_REUSE )",
        "direct ARB2 cache-reuse mask failover",
    )
    require_order(
        reuse_path,
        (
            "RB_ShadowMapPrepareMappedReceiverStencil(",
            "const bool maskOk = receiverStencilReady",
            "if ( maskOk )",
            "RB_ShadowMapMarkStencilFallbackSticky( vLight );",
            "RB_ShadowMapStencilFallback(",
        ),
        "hybrid cache reuse falls back to full stencil if the mapped mask fails",
    )
    fresh_mask_start = direct_shadow_pass.find("bool maskOk = false;")
    if fresh_mask_start < 0:
        raise AssertionError("Missing fresh direct ARB2 receiver-mask result")
    require_order(
        direct_shadow_pass[fresh_mask_start:],
        (
            "RB_ShadowMapPrepareMappedReceiverStencil(",
            "maskOk = receiverStencilReady",
            "const bool mapped =",
            "if ( mapped )",
            "RB_ShadowMapMarkStencilFallbackSticky( vLight );",
            "RB_ShadowMapStencilFallback(",
        ),
        "fresh hybrid map mask failure falls back to full stencil",
    )
    direct_entry = braced_body(
        gl,
        "void RB_ARB2_DrawInteractions( void )",
        "direct ARB2 translucent hybrid handoff",
    )
    require_order(
        direct_entry,
        (
            "g_shadowMapGlobalPassHybrid = false;",
            "RB_ShadowMapRunPass( vLight, SHADOWMAP_PASS_GLOBAL",
            "g_shadowMapGlobalPassMapped != SHADOWMAP_GLOBAL_MAPPED_NONE",
            "RB_ShadowMapPrepareMappedReceiverStencil( vLight",
            "SHADOWMAP_PASS_GLOBAL",
            "g_shadowMapGlobalPassHybrid",
            "const bool translucentMaskOk = receiverStencilReady",
            "if ( translucentMaskOk )",
            "else",
            "RB_ShadowMapMarkStencilFallbackSticky( vLight );",
            "RB_ShadowMapStencilFallback( vLight",
        ),
        "translucent mapped receivers restamp hybrid supplements and fail closed",
    )

    require(
        modern_h,
        "unsigned int\t\tarb2CacheKeyHitMask;",
        "modern descriptor canonical cache-key provenance",
    )
    modern_single_resource = braced_body(
        modern,
        "static bool R_ModernShadowPlanner_Arb2SingleResourceComplete(",
        "modern single-resource ownership fail-closed gate",
    )
    require_order(
        modern_single_resource,
        (
            "vLight->localInteractions != NULL",
            "activeReceiverMask |= SHADOWMAP_RECEIVER_MASK_LOCAL;",
            "vLight->globalInteractions != NULL",
            "vLight->translucentInteractions != NULL",
            "activeReceiverMask |= SHADOWMAP_RECEIVER_MASK_GLOBAL;",
            "vLight->shadowMapIncompleteMapMask",
            "vLight->shadowMapHybridIncompleteMask",
            "vLight->shadowMapPrelightMapMissingMask",
            "incompleteMapMask & activeReceiverMask",
            "vLight->localShadowMapCasters != NULL",
            "vLight->localShadowMapDynamicCasters != NULL",
            "vLight->localTranslucentShadowMapCasters != NULL",
            "vLight->globalShadowMapStencilSupplements != NULL",
            "vLight->localShadowMapStencilSupplements != NULL",
            "return activeReceiverMask != 0;",
        ),
        "modern projected and point resources cannot collapse receiver ownership or stencil supplements",
    )

    modern_point_resource = braced_body(
        modern,
        "static void R_ModernShadowPlanner_ResolveArb2AtlasSlot(",
        "modern exact point-cube planner resolution",
    )
    require_order(
        modern_point_resource,
        (
            "descriptor.arb2AtlasSlotReady = false;",
            "descriptor.arb2AtlasSignature = 0;",
            "descriptor.arb2AtlasStorageGeneration = 0;",
            "descriptor.arb2PointCubeReady = false;",
            "descriptor.arb2PointCubeSignature = 0;",
            "descriptor.arb2PointCubeContentFrame = -1;",
            "descriptor.arb2CacheKeyHitMask & SHADOWMAP_ARB2_CACHE_PASS_GLOBAL",
            "vLight->shadowMapDynamicCasterCount == 0",
            "R_ModernShadowPlanner_Arb2SingleResourceComplete( vLight )",
            "if ( descriptor.pointLight )",
            "RB_ShadowMapPointCubeForLight( vLight, viewDef, cube )",
            "cube.size != descriptor.resolution",
            "descriptor.arb2PointCubeSignature = cube.signature;",
            "descriptor.arb2PointCubeContentFrame = cube.lastUpdatedFrame;",
            "descriptor.arb2PointCubeReady = true;",
            "RB_ShadowMapProjectedAtlasSlotForLight( vLight, viewDef, slot )",
            "descriptor.arb2AtlasSignature = slot.signature;",
            "descriptor.arb2AtlasStorageGeneration = slot.storageGeneration;",
            "descriptor.arb2AtlasContentFrame = slot.lastUpdatedFrame;",
            "descriptor.arb2AtlasSlotReady = true;",
        ),
        "modern descriptors require exact complete static GLOBAL physical content",
    )
    require(
        modern_h,
        "arb2AtlasStorageGeneration;",
        "modern projected-atlas generation provenance ABI",
    )

    modern_cache_isolation = braced_body(
        modern,
        "static bool R_ModernShadowPlanner_CanIsolateArb2CacheOwnership(",
        "modern physical shadow resource isolation",
    )
    require_order(
        modern_cache_isolation,
        (
            "descriptor.pointLight",
            "descriptor.arb2PointCubeReady",
            "descriptor.arb2AtlasSlotReady",
            "descriptor.modernReceiverSamplingReady && !physicalResourceReady",
        ),
        "cache isolation follows point-cube or projected-atlas provenance",
    )

    vk_backend = read("src/renderer/Vulkan/vk_Backend.cpp")
    vk_projected_slot_stub = braced_body(
        vk_backend,
        "bool RB_ShadowMapProjectedAtlasSlotForLight(",
        "Vulkan fail-closed GL projected-slot provenance stub",
    )
    require_order(
        vk_projected_slot_stub,
        (
            "(void)vLight; (void)viewDef;",
            "memset( &slot, 0, sizeof( slot ) );",
            "return false;",
        ),
        "Vulkan must not advertise the GL projected atlas",
    )
    vk_projected_pin_stub = braced_body(
        vk_backend,
        "bool RB_ShadowMapProjectedAtlasSlotMarkUsed(",
        "Vulkan fail-closed GL projected-slot pin stub",
    )
    require_order(
        vk_projected_pin_stub,
        (
            "(void)lightDefIndex; (void)signature; (void)cellX; (void)cellY;",
            "(void)storageGeneration; (void)cellSpan;",
            "return false;",
        ),
        "Vulkan projected-slot pin cannot validate GL residency",
    )
    vk_point_cube_stub = braced_body(
        vk_backend,
        "bool RB_ShadowMapPointCubeForLight(",
        "Vulkan fail-closed GL point-cube provenance stub",
    )
    require_order(
        vk_point_cube_stub,
        (
            "memset( &cube, 0, sizeof( cube ) );",
            "return false;",
        ),
        "Vulkan must not advertise the GL point cube",
    )
    vk_point_mark_stub = braced_body(
        vk_backend,
        "void RB_ShadowMapPointCubeMarkUsed(",
        "Vulkan no-op GL point-cube residency stub",
    )
    require(vk_point_mark_stub, "(void)lightDefIndex;", "Vulkan point-cube pin is a no-op")

    clustered = read("src/renderer/ModernClusteredLighting.cpp")
    clustered_flags = braced_body(
        clustered,
        "static int R_ModernClusteredLighting_ShadowDescriptorFlags(",
        "clustered shadow physical-resource flag",
    )
    require(
        clustered_flags,
        "shadow.arb2AtlasSlotReady || shadow.arb2PointCubeReady",
        "point cubes and projected atlas cells share the GPU provenance flag",
    )
    clustered_copy = braced_body(
        clustered,
        "static void R_ModernClusteredLighting_CopyPlannerShadowDescriptor(",
        "clustered point-cube freshness handoff",
    )
    require_order(
        clustered_copy,
        (
            "src.pointLight",
            "src.arb2PointCubeContentFrame",
            "src.arb2AtlasContentFrame",
            "src.arb2AtlasSlotReady || src.arb2PointCubeReady",
        ),
        "clustered descriptors carry exact point or projected resource freshness",
    )
    clustered_apply = braced_body(
        clustered,
        "static void R_ModernClusteredLighting_ApplyShadowDescriptor(",
        "clustered per-light physical-resource gate",
    )
    require_order(
        clustered_apply,
        (
            "bool projectedSlotReady = shadow->arb2AtlasSlotReady;",
            "RB_ShadowMapProjectedAtlasSlotMarkUsed(",
            "shadow->lightDefIndex, shadow->arb2AtlasSignature",
            "shadow->arb2AtlasStorageGeneration",
            "shadow->arb2AtlasCellX, shadow->arb2AtlasCellY",
            "shadow->arb2AtlasCellSpan",
            "const bool projectedSlotBlocked",
            "projectedSlotBlocked || pointCubeBlocked",
            "stats.shadowAtlasSlotBlockedLights++;",
        ),
        "projected resource identity is revalidated before modern consumption",
    )
    require_order(
        clustered_apply,
        (
            "const bool projectedSlotBlocked",
            "const bool pointCubeBlocked",
            "!shadow->arb2PointCubeReady",
            "projectedSlotBlocked || pointCubeBlocked",
            "MODERN_SHADOW_FALLBACK_RESOURCE_UNAVAILABLE",
            "RB_ShadowMapPointCubeMarkUsed( shadow->lightDefIndex )",
        ),
        "clustered point lights fail closed without their exact cube",
    )
    clustered_lossless = braced_body(
        clustered,
        "bool R_ModernClusteredLighting_FrameLossless(",
        "clustered frame lossless physical-resource guard",
    )
    require(
        clustered_lossless,
        "rg_clusteredLightingStats.shadowAtlasSlotBlockedLights == 0",
        "slot pin/bind failure blocks visible modern ownership",
    )
    gl_cache_gate = braced_body(
        gl,
        "static bool RB_ShadowMapStaticCacheable(",
        "OpenGL mutable-caster cache gate",
    )
    gl_cache_read_gate = braced_body(
        gl,
        "static bool RB_ShadowMapStaticCacheableReadOnly(",
        "OpenGL mutable-caster read gate",
    )
    for gate in (gl_cache_gate, gl_cache_read_gate):
        require(gate, "vLight->shadowMapStaticCasterCount <= 0", "only opaque static depth is resident")
        if "shadowMapAlphaCasterCount > 0" in gate:
            raise AssertionError("Live cutouts must not disable opaque projected cache composition")
    interaction = read("src/renderer/Interaction.cpp")
    classification = braced_body(interaction, "static bool R_ShadowMapCasterIsDynamic(", "live caster classification")
    require_compact(classification, "if ( shader != NULL && shader->Coverage() == MC_PERFORATED ) { return true; }",
                    "animated cutout coverage is always live")
    require(interaction, "R_ShadowMapCasterIsDynamic( entityDef, shadowShader )", "cutout chain routing")
    require(interaction, "R_ShadowMapCasterIsDynamic( entityDef, shader )", "cutout signature exclusion")
    stats_reset = braced_body(gl, "static void RB_ShadowMapStatsReset( void )", "shadow frame reset")
    require(stats_reset, "RB_ShadowMapPrepareCacheView( backEnd.viewDef );", "per-view cache scope check")

    modern_cache_owner = braced_body(
        modern_executor,
        "static const viewDef_t *R_ModernGLExecutor_ShadowCacheOwnerView(",
        "modern OpenGL shadow cache owner selection",
    )
    require_order(
        modern_cache_owner,
        (
            "packetFrame.NumScenes() - 1",
            "viewDef->renderWorld == NULL",
            "!viewDef->isSubview",
            "return fallback;",
        ),
        "modern OpenGL root-view cache owner selection",
    )
    modern_prepare = braced_body(
        modern_executor,
        "void R_ModernGLExecutor_PrepareFrame(",
        "modern OpenGL pre-plan cache transition",
    )
    require_order(
        modern_prepare,
        (
            "R_ModernGLExecutor_ShadowCacheOwnerView( packetFrame )",
            "RB_ShadowMapPrepareCacheView( shadowCacheOwnerView )",
            "R_ModernGLExecutor_ResetPassOwnershipTable( \"frame-start\" )",
            "R_ModernShadowPlanner_PrepareFrame( packetFrame, shadowPlanningRequested );",
        ),
        "modern OpenGL cache transition precedes planning",
    )
    require_compact(
        modern_prepare,
        """rg_modernGLShadowTextureBindingsCurrent = shadowCacheOwnerView != NULL
            && !RB_ShadowMapPrepareCacheView( shadowCacheOwnerView );""",
        "modern GL first-frame and map-transition binding freshness assignment",
    )
    modern_shadow_bind_marker = "static bool R_ModernGLExecutor_BindModernShadowTextures("
    modern_shadow_bind_definition = modern_executor.find(
        modern_shadow_bind_marker,
        modern_executor.find(modern_shadow_bind_marker) + len(modern_shadow_bind_marker),
    )
    if modern_shadow_bind_definition < 0:
        raise AssertionError("Missing modern OpenGL shadow binding definition")
    modern_shadow_bindings = braced_body(
        modern_executor[modern_shadow_bind_definition:],
        modern_shadow_bind_marker,
        "modern OpenGL transition-frame binding guard",
    )
    require_order(
        modern_shadow_bindings,
        (
            "RB_ShadowMapTextureBindings( bindings );",
            "R_ModernGLExecutor_PointCubeDescriptorReady( bindings )",
            "if ( !rg_modernGLShadowTextureBindingsCurrent )",
            "bindings.projectedPersistentAtlas.ready = false;",
            "bindings.pointAtlas.ready = false;",
            "bindings.projectedMoments[i].ready = false;",
            "bindings.pointMoments[i].ready = false;",
            "if ( !exactPointCubeReady )",
            "if ( bindings.projectedPersistentAtlasReady )",
        ),
        "modern OpenGL transition frame cannot expose stale atlas resources",
    )
    modern_exact_point_mismatch = braced_body(
        modern_shadow_bindings,
        "if ( !exactPointCubeReady )",
        "modern OpenGL exact point-cube mismatch clear",
    )
    require_order(
        modern_exact_point_mismatch,
        (
            "bindings.pointAtlas.ready = false;",
            "bindings.pointAtlasReady = false;",
            "for ( int i = 0; i < RENDERER_SHADOW_TEXTURE_MOMENT_COUNT; ++i )",
            "bindings.pointMoments[i].ready = false;",
            "bindings.pointMomentsReady = false;",
        ),
        "point cube and moments fail closed together on exact-descriptor mismatch",
    )

    modern_point_descriptor_gate = braced_body(
        modern_executor,
        "static bool R_ModernGLExecutor_PointCubeDescriptorReady(",
        "modern GL exact point-cube descriptor gate",
    )
    require_order(
        modern_point_descriptor_gate,
        (
            "bindings.pointAtlasReady",
            "R_ModernShadowPlanner_DescriptorByIndex(",
            "descriptor->pointLight",
            "descriptor->arb2PointCubeReady",
            "bindings.pointAtlasLightIndex == descriptor->lightDefIndex",
            "bindings.pointAtlasSignature",
            "descriptor->arb2PointCubeSignature",
            "bindings.pointAtlasContentFrame",
            "descriptor->arb2PointCubeContentFrame",
            "bindings.pointAtlas.width == descriptor->resolution",
            "bindings.pointAtlas.height == descriptor->resolution",
            "descriptor->modernReceiverSamplingReady",
            "descriptor->policy == MODERN_SHADOW_POLICY_MAPPED",
            "descriptor->policy == MODERN_SHADOW_POLICY_CACHE_REUSE",
        ),
        "modern GL advertises a point cube only for a consumable exact descriptor",
    )
    modern_single_point_cube = braced_body(
        modern_executor,
        "static bool R_ModernGLExecutor_ModernVisibleShadowReceiversReady(",
        "modern GL single point-cube ownership constraint",
    )
    duplicate_point_descriptor = braced_body(
        modern_single_point_cube,
        "if ( distinctPointLightDefs[i] == descriptor->lightDefIndex )",
        "modern GL duplicate point-light descriptor deduplication",
    )
    require_order(
        duplicate_point_descriptor,
        ("seen = true;", "break;"),
        "duplicate descriptors for one point light share its cube",
    )
    unseen_point_descriptor = braced_body(
        modern_single_point_cube,
        "if ( !seen )",
        "modern GL distinct point-light accounting",
    )
    require_order(
        unseen_point_descriptor,
        (
            "distinctPointLightDefCount < static_cast<int>( sizeof( distinctPointLightDefs ) / sizeof( distinctPointLightDefs[0] ) )",
            "distinctPointLightDefs[distinctPointLightDefCount++] = descriptor->lightDefIndex;",
            "distinctPointLightOverflow = true;",
            "consumablePointLights++;",
        ),
        "only distinct point lights consume the single-cube budget and overflow fails closed",
    )
    if modern_single_point_cube.count("consumablePointLights++") != 1:
        raise AssertionError(
            "Point-cube ownership must count each distinct light exactly once"
        )
    point_cube_overflow = braced_body(
        modern_single_point_cube,
        "if ( consumablePointLights > 1 || distinctPointLightOverflow )",
        "modern GL second-point-light fail-closed gate",
    )
    require_order(
        point_cube_overflow,
        (
            "blockedLights += Max( 1, consumablePointLights - 1 );",
            "stats.modernVisibleShadowPointConstraintLights = consumablePointLights;",
        ),
        "multiple distinct point lights cannot share the one bound samplerCube",
    )
    require_order(
        modern_single_point_cube,
        (
            "if ( consumablePointLights > 1 || distinctPointLightOverflow )",
            "stats.modernVisibleShadowBlockedLights = blockedLights;",
            "if ( blockedLights > 0 )",
            "return false;",
        ),
        "point-cube constraint blocks modern visible ownership",
    )
    require(
        modern_shader,
        "mapType == MODERN_SHADOW_MAP_POINT && ModernClusterShadowFlag(descriptor, MODERN_SHADOW_FLAG_ATLAS_SLOT) && descriptor.freshness.y > 0.5",
        "modern point shader rejects missing or stale cube provenance",
    )

    atlas_grid = braced_body(gl, "static int RB_ShadowMapAtlasGridDim( void )", "physical atlas grid")
    require_order(
        atlas_grid,
        (
            "g_shadowMapAtlasRenderTexture->GetWidth()",
            "g_shadowMapAtlasRenderTexture->GetHeight()",
            "atlasSize / g_shadowMapAtlasCellSize",
            "gridDim > 0",
        ),
        "physical atlas grid bounds",
    )

    def atlas_grid_dim(width: int, height: int, cell: int) -> int:
        if cell <= 0:
            return 0
        raw = min(width, height) // cell
        return min(8, max(1, raw)) if raw > 0 else 0

    atlas_cases = {
        (4096, 2048, 512): 4,
        (4095, 4096, 512): 7,
        (256, 512, 512): 0,
        (8192, 8192, 512): 8,
        (4096, 4096, 0): 0,
    }
    for inputs, expected in atlas_cases.items():
        actual = atlas_grid_dim(*inputs)
        if actual != expected:
            raise AssertionError(
                f"Physical atlas grid case {inputs} produced {actual}, expected {expected}"
            )

    atlas_find = braced_body(
        gl,
        "static bool RB_ShadowMapAtlasFindFreeBlock(",
        "bounded atlas block allocation",
    )

    cache_expire = braced_body(
        gl,
        "static void RB_ShadowMapExpireCaches( void )",
        "live OpenGL cache limit enforcement",
    )
    require_order(
        cache_expire,
        (
            "RB_ShadowMapProjectedCacheSlotLimit()",
            "RB_ShadowMapPointCacheSlotLimit()",
            "RB_ShadowMapProjectedCacheEntryStorageValid(",
            "i >= projectedLimit",
            "g_projectedShadowMapCache[i].valid = false;",
            "RB_ShadowMapPointCacheEntryStorageValid(",
            "i >= pointLimit",
            "g_pointShadowMapCache[i].valid = false;",
        ),
        "live OpenGL cache generation, limit, and residency enforcement",
    )
    cache_estimate_counts = braced_body(
        gl,
        "static void RB_ShadowMapArb2CacheSlotCountsReadOnly(",
        "ARB2 cache-estimate live-storage counts",
    )
    require_order(
        cache_estimate_counts,
        (
            "RB_ShadowMapProjectedCacheEntryStorageValid(",
            "estimate.projectedCacheSlotsUsed++;",
            "RB_ShadowMapPointCacheEntryStorageValid(",
            "estimate.pointCacheSlotsUsed++;",
        ),
        "ARB2 cache estimates exclude generation-stale metadata",
    )
    for stale_flag in (
        "g_projectedShadowMapCache[i].valid",
        "g_pointShadowMapCache[i].valid",
    ):
        if stale_flag in cache_estimate_counts:
            raise AssertionError(
                "ARB2 cache estimates must validate physical storage, not raw cache metadata: "
                + stale_flag
            )
    require_order(
        atlas_find,
        (
            "span <= 0 || span > gridDim",
            "y + span <= gridDim",
            "x + span <= gridDim",
            "occupied[y + by][x + bx]",
        ),
        "bounded atlas block allocation",
    )
    atlas_live_occupancy = braced_body(
        atlas_find,
        "if ( !RB_ShadowMapProjectedCacheEntryStorageValid( &entry ) )",
        "projected-atlas live-storage occupancy gate",
    )
    require(
        atlas_live_occupancy,
        "continue;",
        "stale projected-atlas generation excluded from occupancy",
    )
    require_order(
        atlas_find,
        (
            "RB_ShadowMapProjectedCacheEntryStorageValid( &entry )",
            "occupied[y][x] = true;",
            "occupied[y + by][x + bx]",
        ),
        "only live projected-atlas storage reserves cells before free-block search",
    )

    timed_begin = braced_body(
        gl,
        "static void RB_ShadowMapBeginTimedPhase(",
        "non-nested OpenGL shadow timing begin",
    )
    require_order(
        timed_begin,
        (
            "RB_ShadowMapGpuTimerQueriesAvailable()",
            "R_RendererMetrics_PauseGpuTimer( timerSlot )",
            "RB_ShadowMapBeginGpuTimerQuery(",
            "timedPhase.gpuTimerQuery == NULL && timedPhase.parentGpuTimerPaused",
            "R_RendererMetrics_ResumeGpuTimer(",
        ),
        "non-nested OpenGL shadow timing begin",
    )
    timed_end = braced_body(
        gl,
        "static float RB_ShadowMapEndTimedPhase(",
        "non-nested OpenGL shadow timing end",
    )
    require_order(
        timed_end,
        (
            "RB_ShadowMapEndGpuTimerQuery( timedPhase.gpuTimerQuery );",
            "if ( timedPhase.parentGpuTimerPaused )",
            "R_RendererMetrics_ResumeGpuTimer(",
        ),
        "inner query completion before parent timer resume",
    )


def validate_exact_static_cache_and_admission_contract() -> None:
    header = read("src/renderer/Vulkan/vk_ShadowMap.h")
    shadow_map = read("src/renderer/Vulkan/vk_ShadowMap.cpp")

    resources_known_good = braced_body(
        shadow_map,
        "bool VK_ShadowMap_ResourcesKnownGood(",
        "Vulkan shadow resource truth",
    )
    require_order(
        resources_known_good,
        (
            "vkShadowProjectedResourcesOkGeneration != tr.videoRestartCount",
            "return false;",
            """pointLight
                && vkShadowPointResourcesOkGeneration
                    != tr.videoRestartCount""",
            "return false;",
        ),
        "generation-gated Vulkan shadow resource truth",
    )
    # The front-end elision gate (R_ShadowMapLightWillUseShadowMaps) may only
    # shed a light's stencil volumes once both class generations prove out, so
    # the two guarded falses must precede the single trailing true.
    if resources_known_good.count("return false;") != 2 or not compact(
        resources_known_good
    ).endswith("return true; }"):
        raise AssertionError(
            "Vulkan resource truth must report generation truth: two guarded falses then true"
        )

    # A per-view admission miss lands after the front end has already elided
    # volumes, so every unresolved receiver must still draw unshadowed. The
    # 2026-07-24 fail-closed shape (dropping the light contribution) is what
    # forced the conservative gate; it must not return.
    interactions = read("src/renderer/Vulkan/vk_Interactions.cpp")
    for unresolved_receiver_fallback in (
        """if ( !localReceiverDrawn && vLight->localInteractions != NULL ) {""",
        """if ( !globalOpaqueReceiverDrawn
            && vLight->globalInteractions != NULL ) {""",
        "translucentUsesUnshadowedFallback",
    ):
        require_compact(
            interactions,
            unresolved_receiver_fallback,
            "unresolved Vulkan receivers draw unshadowed instead of vanishing",
        )

    require_compact(
        header,
        """static const int VK_SHADOW_MAX_POINT_CUBES =
            VK_SHADOW_MAX_LIGHTS * VK_SHADOW_RECEIVER_PASS_COUNT;""",
        "point scratch capacity for every admitted receiver ownership",
    )
    require_compact(
        header,
        "static const int VK_SHADOW_MAX_CACHE_SLOTS = 16;",
        "bounded class-specific static caches",
    )
    for obsolete_gate in (
        "VK_SHADOW_MAX_POINT_LIGHTS",
        "pointLightsUsed",
    ):
        if obsolete_gate in header or obsolete_gate in shadow_map:
            raise AssertionError(
                f"Vulkan point admission must not retain the old arbitrary light gate {obsolete_gate!r}"
            )

    projected_entry = braced_body(
        shadow_map,
        "typedef struct vkProjectedShadowCacheEntry_s",
        "projected static-cache entry",
    )
    require_order(
        projected_entry,
        (
            "bool valid;",
            "bool reserved;",
            "int generation;",
            "const idRenderWorldLocal *renderWorld;",
            "int lightIndex;",
            "vkShadowReceiverPass_t passKind;",
            "int signature;",
            "int tileSize;",
            "int lastUsedFrame;",
            "shadowMapProjectedLightState_t projectedState;",
            "VkImage image;",
            "VkImageLayout layout;",
        ),
        "projected exact resident metadata",
    )
    point_entry = braced_body(
        shadow_map,
        "typedef struct vkPointShadowCacheEntry_s",
        "point static-cache entry",
    )
    require_order(
        point_entry,
        (
            "bool valid;",
            "bool reserved;",
            "int generation;",
            "const idRenderWorldLocal *renderWorld;",
            "int lightIndex;",
            "vkShadowReceiverPass_t passKind;",
            "int signature;",
            "int size;",
            "int lastUsedFrame;",
            "float pointFar;",
            "float lightOrigin[ 3 ];",
            "vkPointShadowCube_t cube;",
        ),
        "point exact resident metadata",
    )

    state = braced_body(
        shadow_map,
        "typedef struct vkShadowMapState_s",
        "Vulkan shadow-map state",
    )
    require_order(
        state,
        (
            "vkPointShadowCube_t pointCubes[ VK_SHADOW_MAX_POINT_CUBES ];",
            "vkProjectedShadowCacheEntry_t projectedCache[ VK_SHADOW_MAX_CACHE_SLOTS ];",
            "vkPointShadowCacheEntry_t pointCache[ VK_SHADOW_MAX_CACHE_SLOTS ];",
            "const idRenderWorldLocal *cacheRenderWorld;",
            "unsigned int cacheMapFileCRC;",
            "int cacheMapNameHash;",
            "int pointCubesUsed;",
            "int freshUpdates;",
        ),
        "separate scratch, projected-resident, and point-resident storage",
    )

    signature = braced_body(
        shadow_map,
        "static int VK_ShadowMap_BuildPassSignatureForView(",
        "exact shadow-cache signature",
    )
    for token, context in (
        ("viewDef != NULL ? viewDef->renderWorld : NULL", "render-world identity"),
        ("viewDef->renderWorld->mapFileCRC", "map-file identity"),
        ("VK_ShadowMap_MapNameHash( viewDef )", "map-name identity"),
        ("VK_ShadowMap_LightIndex( vLight )", "light identity"),
        ("static_cast<int>( passKind )", "ownership identity"),
        ("static_cast<int>( classification.lightClass )", "light-class identity"),
        ("vLight->shadowMapCasterSignature", "caster-content identity"),
        ("resourceSize", "resource-size identity"),
        ("R_ShadowMapPointFarDistance( vLight )", "point far-envelope identity"),
        ("vLight->lightDef->parms.lightCenter[ i ]", "point center identity"),
        ("vLight->globalLightOrigin[ i ]", "receiver light-origin identity"),
        ("vLight->lightRadius[ i ]", "light-radius identity"),
        ("vLight->lightProject[ planeIndex ][ component ]", "projected-light identity"),
    ):
        require(signature, token, f"exact shadow-cache {context}")

    projected_find = braced_body(
        shadow_map,
        "static int VK_ShadowMap_FindProjectedCacheEntry(",
        "exact projected cache lookup",
    )
    require_order(
        projected_find,
        (
            "entry.valid && !entry.reserved",
            "entry.generation == tr.videoRestartCount",
            "entry.renderWorld == renderWorld",
            "entry.lightIndex == lightIndex",
            "entry.passKind == passKind",
            "entry.signature == signature",
            "entry.tileSize == tileSize",
            "entry.image != VK_NULL_HANDLE",
            "entry.layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL",
            "entry.lastUsedFrame = tr.frameCount;",
            "entry.reserved = true;",
            "return i;",
        ),
        "exact projected cache lookup and hit reservation",
    )
    point_find = braced_body(
        shadow_map,
        "static int VK_ShadowMap_FindPointCacheEntry(",
        "exact point cache lookup",
    )
    require_order(
        point_find,
        (
            "entry.valid && !entry.reserved",
            "entry.generation == tr.videoRestartCount",
            "entry.renderWorld == renderWorld",
            "entry.lightIndex == lightIndex",
            "entry.passKind == passKind",
            "entry.signature == signature",
            "entry.size == size",
            "entry.cube.image != VK_NULL_HANDLE",
            """entry.cube.layout
                == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL""",
            "entry.lastUsedFrame = tr.frameCount;",
            "entry.reserved = true;",
            "return i;",
        ),
        "exact point cache lookup and hit reservation",
    )

    lookup_definitions = [
        line.strip().split("(", 1)[0].split()[-1]
        for line in shadow_map.splitlines()
        if line.startswith("static int VK_ShadowMap_Find") and "Cache" in line
    ]
    if lookup_definitions != [
        "VK_ShadowMap_FindProjectedCacheEntry",
        "VK_ShadowMap_FindPointCacheEntry",
    ]:
        raise AssertionError(
            f"Vulkan cache scheduling must expose only exact class lookups; got {lookup_definitions!r}"
        )
    for stale_identifier in (
        "FindAny",
        "FindStale",
        "AllowStale",
        "allowStale",
        "anySignature",
        "staleSignature",
    ):
        if stale_identifier in shadow_map:
            raise AssertionError(
                f"Vulkan cache scheduling must not contain stale/any-signature path {stale_identifier!r}"
            )

    static_gate = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_StaticCacheable(",
        "opaque static-only cache gate",
    )
    require_compact(
        static_gate,
        """const bool haveDynamicCasters = vLight != NULL
            && ( vLight->shadowMapDynamicCasterCount > 0
                || vLight->globalShadowMapDynamicCasters != NULL
                || vLight->localShadowMapDynamicCasters != NULL );""",
        "dynamic caster cache exclusion",
    )
    for exclusion in (
        "dynamicsDefeatCache",
        "vLight->shadowMapCasterCount <= 0",
        "vLight->shadowMapStaticCasterCount <= 0",
        "vLight->shadowMapTranslucentCasterCount > 0",
        "vLight->globalTranslucentShadowMapCasters != NULL",
        "vLight->localTranslucentShadowMapCasters != NULL",
    ):
        require(static_gate, exclusion, f"opaque static-only exclusion {exclusion}")
    # GL parity: view-fitted CSM reuse is an opt-in cvar gate, not a
    # structural exclusion. AllocateProjectedPass restores the resident fit
    # along with the tiles. The exact signature also includes that fit;
    # copying cached depth still has a cost even when its coverage is current.
    require_compact(
        static_gate,
        """if ( !pointLight && cascadeCount > 1
            && !r_shadowMapCacheCSM.GetBool() ) {
            return false;
        }""",
        "projected CSM cache gate",
    )
    require(
        static_gate,
        "r_shadowMapStaticHysteresisFrames.GetInteger()",
        "static-cache dynamic hysteresis",
    )

    projected_limit = braced_body(
        shadow_map,
        "static int VK_ShadowMap_ProjectedCacheSlotLimit(",
        "projected cache-size policy",
    )
    point_limit = braced_body(
        shadow_map,
        "static int VK_ShadowMap_PointCacheSlotLimit(",
        "point cache-size policy",
    )
    require(
        projected_limit,
        "r_shadowMapProjectedCacheSize.GetInteger()",
        "projected cache-size cvar",
    )
    require(
        point_limit,
        "r_shadowMapPointCacheSize.GetInteger()",
        "point cache-size cvar",
    )

    begin_cache_view = braced_body(
        shadow_map,
        "static void VK_ShadowMap_BeginCacheView(",
        "per-view cache invalidation",
    )
    require_order(
        begin_cache_view,
        (
            "renderWorld->mapFileCRC",
            "VK_ShadowMap_MapNameHash( viewDef )",
            "VK_ShadowMap_ProjectedCacheSlotLimit()",
            "VK_ShadowMap_PointCacheSlotLimit()",
            "r_shadowMapResidentFrames.GetInteger()",
            "vkShadow.cacheRenderWorld != renderWorld",
            "vkShadow.cacheMapFileCRC != mapFileCRC",
            "vkShadow.cacheMapNameHash != mapNameHash",
            "VK_ShadowMap_ClearProjectedEntryMetadata(",
            "VK_ShadowMap_ClearPointEntryMetadata(",
            "projected.reserved = false;",
            "i >= projectedLimit",
            "projected.generation != tr.videoRestartCount",
            "tr.frameCount - projected.lastUsedFrame",
            "point.reserved = false;",
            "i >= pointLimit",
            "point.generation != tr.videoRestartCount",
            "tr.frameCount - point.lastUsedFrame",
        ),
        "map/generation/residency cache invalidation",
    )

    cache_pass_kind = braced_body(
        shadow_map,
        "static vkShadowReceiverPass_t VK_ShadowMap_CachePassKind(",
        "LOCAL/GLOBAL cache canonicalization",
    )
    require_compact(
        cache_pass_kind,
        """vLight->localShadowMapCasters == NULL
            && vLight->localShadowMapDynamicCasters == NULL
            && vLight->localTranslucentShadowMapCasters == NULL""",
        "canonicalization across every local caster class",
    )
    require_order(
        cache_pass_kind,
        (
            "vLight->localTranslucentShadowMapCasters == NULL",
            "return VK_SHADOW_RECEIVER_GLOBAL;",
            "return requestedPass;",
        ),
        "safe LOCAL/GLOBAL canonical identity",
    )
    has_local_casters = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_HasLocalCasters(",
        "LOCAL/GLOBAL runtime resource alias",
    )
    require_compact(
        has_local_casters,
        """return vLight->localShadowMapCasters != NULL
            || vLight->localShadowMapDynamicCasters != NULL
            || vLight->localTranslucentShadowMapCasters != NULL;""",
        "runtime alias check across every local caster class",
    )

    for marker, context, ensure_call, cache_name in (
        (
            "static int VK_ShadowMap_AllocProjectedCacheEntry(",
            "projected cache LRU allocation",
            "VK_ShadowMap_EnsureProjectedCacheImage( selected, blockSize )",
            "projectedCache",
        ),
        (
            "static int VK_ShadowMap_AllocPointCacheEntry(",
            "point cache LRU allocation",
            "VK_ShadowMap_EnsurePointCacheCube( selected )",
            "pointCache",
        ),
    ):
        allocation = braced_body(shadow_map, marker, context)
        require_order(
            allocation,
            (
                "if ( entry.reserved )",
                "continue;",
                "if ( !entry.valid )",
                "selected = i;",
                "entry.lastUsedFrame",
                f"vkShadow.{cache_name}[ selected ].lastUsedFrame",
                ensure_call,
                f"VK_ShadowMap_Clear{'Projected' if cache_name == 'projectedCache' else 'Point'}EntryMetadata(",
                f"vkShadow.{cache_name}[ selected ].reserved = true;",
                "return selected;",
            ),
            context,
        )

    schedule = braced_body(
        shadow_map,
        "static vkShadowSchedule_t VK_ShadowMap_SchedulePass(",
        "exact cache and fresh-update admission",
    )
    require_compact(
        schedule,
        """const int incompleteStencilMask =
            vLight->shadowMapIncompleteStencilMask |
            ( vLight->shadowMapPrelightStencilRequiredMask
                & ~vLight->shadowMapPrelightStencilReadyMask );
        const bool mapRequiredForCorrectness =
            !stencilFallbackAvailable ||
            ( incompleteStencilMask & receiverMask ) != 0;""",
        "map-only and stencil-less-target correctness override",
    )
    policy2_position = schedule.find("subviewPolicy >= 2")
    cache_gate_position = schedule.find("schedule.cacheable =")
    lookup_position = schedule.find("if ( schedule.cacheable )")
    policy1_position = schedule.find("subviewPolicy >= 1")
    budget_position = schedule.find("const int updateBudget")
    fresh_position = schedule.find("vkShadow.freshUpdates++;")
    cache_alloc_position = schedule.rfind("if ( schedule.cacheable )")
    if not (
        0 <= policy2_position < cache_gate_position <= lookup_position
        < policy1_position < budget_position < fresh_position < cache_alloc_position
    ):
        raise AssertionError(
            "Subview policy 2, exact lookup, policy 1 fallback, budget, and fresh admission are out of order"
        )
    policy2_block = schedule[policy2_position:cache_gate_position]
    exact_lookup_block = schedule[lookup_position:policy1_position]
    policy1_block = schedule[policy1_position:budget_position]
    budget_block = schedule[budget_position:fresh_position]
    for block, context in (
        (policy2_block, "subview policy 2"),
        (policy1_block, "subview policy 1 miss"),
        (budget_block, "fresh-update budget miss"),
    ):
        require(
            block,
            "schedule.action = VK_SHADOW_SCHEDULE_FALLBACK;",
            context,
        )
        require(block, "return schedule;", context)
        require(
            block,
            "!mapRequiredForCorrectness",
            f"{context} correctness override",
        )
    require(
        exact_lookup_block,
        "VK_ShadowMap_FindPointCacheEntry(",
        "point exact lookup before subview policy 1 fallback",
    )
    require(
        exact_lookup_block,
        "VK_ShadowMap_FindProjectedCacheEntry(",
        "projected exact lookup before subview policy 1 fallback",
    )
    require_order(
        exact_lookup_block,
        (
            "if ( schedule.cacheEntry >= 0 )",
            "schedule.action = VK_SHADOW_SCHEDULE_REUSE;",
            "return schedule;",
        ),
        "exact hit admission before subview/budget fallback",
    )
    if schedule.count("VK_ShadowMap_Find") != 2:
        raise AssertionError("Vulkan scheduling must perform exactly the two exact class lookups")
    if schedule.count("vkShadow.freshUpdates++;") != 1:
        raise AssertionError("A fresh ownership map must consume the update budget exactly once")
    if "VK_ShadowMap_MarkStencilFallbackSticky" in schedule:
        raise AssertionError("Subview/budget cache misses must not become sticky")
    if "VK_SHADOW_SCHEDULE_FALLBACK" in schedule[cache_alloc_position:]:
        raise AssertionError("Optional cache-allocation failure must remain a fresh uncached update")

    prepare = braced_body(
        shadow_map,
        "int VK_ShadowMap_PrepareViewLights(",
        "per-view cache admission",
    )
    require_compact(
        prepare,
        """for ( int correctnessPhase = 0 ; correctnessPhase < 2 ;
                correctnessPhase++ ) {
            const bool requiredPhase = correctnessPhase == 0;""",
        "correctness-required light priority",
    )
    require_compact(
        prepare,
        """const bool passRequiresMap[ VK_SHADOW_RECEIVER_PASS_COUNT ] = {
            !stencilFallbackAvailable ||
                ( incompleteStencilMask &
                    SHADOWMAP_RECEIVER_MASK_LOCAL ) != 0,
            !stencilFallbackAvailable ||
                ( incompleteStencilMask &
                    SHADOWMAP_RECEIVER_MASK_GLOBAL ) != 0
        };""",
        "per-ownership stencil availability override",
    )
    require_compact(
        prepare,
        """if ( lightHasRequiredMap &&
                    !passRequiresMap[ passIndex ] ) {
                continue;
            }""",
        "mixed required/optional ownership admission",
    )
    require_order(
        prepare,
        (
            "const bool requiredPhase = correctnessPhase == 0;",
            "const bool lightHasRequiredMap =",
            "if ( lightHasRequiredMap != requiredPhase )",
            "if ( vkShadow.numLights >= VK_SHADOW_MAX_LIGHTS )",
        ),
        "correctness-required maps before bounded capacity admission",
    )
    require_order(
        prepare,
        (
            "VK_ShadowMap_AliasPass( entry, receiverPass, VK_SHADOW_RECEIVER_LOCAL );",
            "VK_ShadowMap_SchedulePass(",
            "VK_ShadowMap_AliasPass( entry, receiverPass, VK_SHADOW_RECEIVER_LOCAL );",
            "VK_ShadowMap_SchedulePass(",
        ),
        "ownership aliases before point/projected budget admission",
    )
    point_section_start = prepare.find("if ( classification.pointLight )")
    projected_section_start = prepare.find("// Match RB_ShadowMapTileSizeForLight")
    if not (0 <= point_section_start < projected_section_start):
        raise AssertionError("Could not isolate point/projected admission branches")
    for section, allocate_call, context in (
        (
            prepare[point_section_start:projected_section_start],
            "VK_ShadowMap_AllocatePointPass(",
            "point admission fallback",
        ),
        (
            prepare[projected_section_start:],
            "VK_ShadowMap_AllocateProjectedPass(",
            "projected admission fallback",
        ),
    ):
        fallback_position = section.find("VK_SHADOW_SCHEDULE_FALLBACK")
        allocation_position = section.find(allocate_call, fallback_position)
        if not (0 <= fallback_position < allocation_position):
            raise AssertionError(f"Could not isolate {context}")
        fallback_block = section[fallback_position:allocation_position]
        require(fallback_block, "continue;", context)
        if "VK_ShadowMap_MarkStencilFallbackSticky" in fallback_block:
            raise AssertionError(f"{context} must retain same-frame stencil without becoming sticky")

    resources = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_EnsureResources(",
        "transfer-capable shadow atlas",
    )
    require_compact(
        resources,
        """ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
            | VK_IMAGE_USAGE_SAMPLED_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            | VK_IMAGE_USAGE_TRANSFER_DST_BIT;""",
        "projected atlas transfer source/destination usage",
    )
    projected_image = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_EnsureProjectedCacheImage(",
        "transfer-only projected resident image",
    )
    require_compact(
        projected_image,
        """ici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            | VK_IMAGE_USAGE_TRANSFER_DST_BIT;""",
        "projected resident transfer usage",
    )
    for forbidden_usage in (
        "VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT",
        "VK_IMAGE_USAGE_SAMPLED_BIT",
    ):
        if forbidden_usage in projected_image:
            raise AssertionError(
                f"Projected resident tiles must stay transfer-only, not {forbidden_usage}"
            )
    require_order(
        projected_image,
        (
            "vmaCreateImage(",
            "entry.layout = VK_IMAGE_LAYOUT_UNDEFINED;",
        ),
        "projected resident initial layout tracking",
    )

    image_barrier = braced_body(
        shadow_map,
        "static void VK_ShadowMap_ImageBarrier(",
        "shadow cache image barriers",
    )
    require_order(
        image_barrier,
        (
            "barrier.oldLayout = oldLayout;",
            "barrier.newLayout = newLayout;",
            "barrier.image = image;",
            "barrier.subresourceRange.aspectMask = VK_ShadowMap_DepthAspectMask();",
            "barrier.subresourceRange.layerCount = layerCount;",
            "vkCmdPipelineBarrier2( cmd, &dep );",
        ),
        "tracked depth-image layout transitions",
    )
    copy_depth = braced_body(
        shadow_map,
        "static void VK_ShadowMap_CopyDepthTile(",
        "projected cache depth copy",
    )
    require_order(
        copy_depth,
        (
            "region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;",
            "region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;",
            "copy.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;",
            "copy.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;",
            "vkCmdCopyImage2( cmd, &copy );",
        ),
        "depth-only projected cache copy",
    )

    render = braced_body(
        shadow_map,
        "bool VK_ShadowMap_RenderAtlas(",
        "cached shadow rendering",
    )
    if render.count("VK_ShadowMap_CopyDepthTile(") != 2:
        raise AssertionError(
            "Projected caching must contain exactly one atlas-to-cache and one cache-to-atlas copy path"
        )
    require_order(
        render,
        (
            "if ( haveCacheUpdates )",
            "VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL",
            """VK_ShadowMap_CopyDepthTile( cmd,
                vkShadow.atlasImage,
                pass.tileX, pass.tileY,
                cache.image, 0, 0,
                VK_ShadowMap_ProjectedBlockSize( light ) );""",
            "cache.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;",
            "if ( haveCacheHits )",
            "VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL",
            """VK_ShadowMap_CopyDepthTile( cmd,
                cache.image, 0, 0,
                vkShadow.atlasImage,
                pass.tileX, pass.tileY,
                VK_ShadowMap_ProjectedBlockSize( light ) );""",
            "VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL",
            "vkShadow.atlasLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;",
        ),
        "projected resident publish/reuse transfers and final sampled layout",
    )
    if compact(render).count(compact("|| !cache->reserved")) != 2:
        raise AssertionError(
            "Projected and point exact hits must both revalidate their per-view reservation"
        )

    ensure_point_scratch = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_EnsurePointCube(",
        "point scratch cube allocation",
    )
    ensure_point_resident = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_EnsurePointCacheCube(",
        "point resident cube allocation",
    )
    require_compact(
        ensure_point_scratch,
        "VK_ShadowMap_CreatePointCube( vkShadow.pointCubes[ index ] )",
        "scratch point cube ownership",
    )
    require_compact(
        ensure_point_resident,
        "VK_ShadowMap_CreatePointCube( vkShadow.pointCache[ index ].cube )",
        "identity-resident point cube ownership",
    )

    allocate_point = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_AllocatePointPass(",
        "point resident-hit and scratch allocation",
    )
    require_order(
        allocate_point,
        (
            "if ( schedule.action == VK_SHADOW_SCHEDULE_REUSE )",
            "vkShadow.pointCache[ schedule.cacheEntry ]",
            "cache.cube.sets[ frameSlot ]",
            "pass.cacheHit = true;",
            "light.tileSize = cache.size;",
            "light.pointFar = cache.pointFar;",
            "light.pointLightOrigin[ i ] = cache.lightOrigin[ i ];",
            "if ( schedule.cacheEntry >= 0 )",
            "pass.cacheUpdate = true;",
            "cache.reserved = false;",
            "vkShadow.pointCubesUsed >= VK_SHADOW_MAX_POINT_CUBES",
            "VK_ShadowMap_EnsurePointCube(",
            "vkShadow.pointCubesUsed++;",
        ),
        "resident point hit state restore before fresh cache/scratch fallback",
    )
    require_compact(
        render,
        """if ( projectedCount == 0 && pointFreshCount == 0
            && pointHitCount > 0 ) {
            VK_ShadowMap_FinalizeCachePasses( viewDef );
            return true;
        }""",
        "identity-resident point-hit view without scratch rendering",
    )

    finalize = braced_body(
        shadow_map,
        "static void VK_ShadowMap_FinalizeCachePasses(",
        "post-render cache metadata publication",
    )
    require_order(
        finalize,
        (
            "if ( pass.cacheHit )",
            "vkShadow.pointCache[",
            "pass.cacheEntry ].reserved = false;",
            "vkShadow.projectedCache[",
            "pass.cacheEntry ].reserved = false;",
            "if ( !pass.cacheUpdate )",
            "cache.cube.layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL",
            "cache.valid = true;",
            "cache.reserved = false;",
            "cache.generation = tr.videoRestartCount;",
            "cache.renderWorld = viewDef->renderWorld;",
            "cache.signature = pass.cacheSignature;",
            "cache.size = light.tileSize;",
            "cache.pointFar = light.pointFar;",
            "cache.lightOrigin[ originIndex ]",
            "cache.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL",
            "cache.valid = true;",
            "cache.reserved = false;",
            "cache.generation = tr.videoRestartCount;",
            "cache.renderWorld = viewDef->renderWorld;",
            "cache.signature = pass.cacheSignature;",
            "cache.tileSize = light.tileSize;",
            "cache.projectedState = light.projectedState;",
        ),
        "successful point/projected cache metadata publication",
    )
    if shadow_map.count("cache.valid = true;") != finalize.count(
        "cache.valid = true;"
    ):
        raise AssertionError(
            "Resident cache metadata must only become valid inside the final publication step"
        )

    resume_position = render.rfind("const bool resumedMainRendering")
    failure_position = render.find("if ( !resumedMainRendering )", resume_position)
    abandon_position = render.find("VK_ShadowMap_AbandonPreparedLights();", failure_position)
    success_position = render.find("} else {", abandon_position)
    finalize_position = render.find(
        "VK_ShadowMap_FinalizeCachePasses( viewDef );",
        success_position,
    )
    if not (
        0 <= resume_position < failure_position < abandon_position
        < success_position < finalize_position
    ):
        raise AssertionError(
            "Fresh cache metadata must publish only after main rendering resumes successfully"
        )


def validate_packed_shadow_geometry() -> None:
    interactions = read("src/renderer/Vulkan/vk_Interactions.cpp")
    header_gate = braced_body(
        interactions,
        "static bool VK_Inter_PackedShadowHeaderValid(",
        "packed shadow header validation",
    )
    require_order(
        header_gate,
        (
            "const int64_t headerWords = static_cast<int64_t>( numPrimBatches ) * 2;",
            "const int64_t requiredWords = static_cast<int64_t>( tri->numIndexes ) + headerWords;",
            "if ( requiredWords > tri->numAllocedIndices )",
            "if ( noCaps < 0 || withCaps < 0",
            "( noCaps % 3 ) != 0",
            "( withCaps % 3 ) != 0",
            "noCaps > withCaps",
            "withCaps > tri->numIndexes - total",
            "return total == tri->numIndexes;",
        ),
        "packed shadow header validation",
    )

    packed_draw = braced_body(
        interactions,
        "static bool VK_Inter_DrawPackedShadowSurface(",
        "packed shadow surface drawing",
    )
    require_compact(
        packed_draw,
        "indexCount = drawCaps ? totalIndexCount : noCaps;",
        "packed shadow cap selection",
    )
    require_compact(
        packed_draw,
        """skinPackedVertices
            && !VK_Inter_MD5RSkinShadowPosition( *vertexBuffer, sourceVertex, batch,
                tri, range.transformBase, position )""",
        "packed shadow CPU skinning",
    )
    require_order(
        packed_draw,
        (
            "for ( int batchIndex = 0 ; batchIndex < numBatches ; batchIndex++ )",
            "idTempArray<shadowCache_t> verts(",
            "idTempArray<glIndex_t> indexes(",
            "VK_Exec_BindRawShadowGeometry(",
            "bool drewAnything = false;",
            "vkCmdDrawIndexed(",
        ),
        "prevalidated packed shadow upload and draw",
    )

    stencil_pass = braced_body(
        interactions,
        "static bool VK_StencilShadowPass(",
        "stencil shadow volume rendering",
    )
    require_order(
        stencil_pass,
        (
            "const bool packedPrimBatches = R_TriHasPrimBatchMesh( tri );",
            "if ( !packedPrimBatches",
            "VK_Exec_BindShadowGeometry( cmd, interPass.slot, tri )",
            "if ( packedPrimBatches )",
            """VK_Inter_DrawPackedShadowSurface( surf, packedCapInclusive, external,
                frontSidedFace, backSidedFace )""",
            "interPass.volumeSkipCount++;",
            "continue;",
        ),
        "packed/classic stencil geometry dispatch",
    )

    executor = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    raw_bind = braced_body(
        executor,
        "bool VK_Exec_BindRawShadowGeometry(",
        "transient packed shadow geometry binding",
    )
    require_order(
        raw_bind,
        (
            "verts == NULL || indexes == NULL || numVerts <= 0 || numIndexes <= 0",
            "VK_Ring_Alloc( vkExec.vertexRings[ slot ], verts,",
            "if ( vertexOffset < 0 )",
            "VK_Ring_Alloc( vkExec.indexRings[ slot ], indexes,",
            "if ( indexOffset < 0 )",
            "vkCmdBindVertexBuffers(",
            "vkCmdBindIndexBuffer(",
        ),
        "transient packed shadow geometry binding",
    )


def validate_fail_closed_target_and_stencil_behavior() -> None:
    render_init = read("src/renderer/RenderSystem_init.cpp")
    gl_backend = read("src/renderer/draw_arb2.cpp")
    require(
        render_init,
        'idCVar r_vkShadowFallbackTest( "r_vkShadowFallbackTest", "0"',
        "default-off Vulkan shadow fallback injection",
    )
    require(
        read("src/renderer/RendererBootstrap.cpp"),
        '{ "r_vkShadowFallbackTest", &r_vkShadowFallbackTest, 0 }',
        "default-safety Vulkan shadow fallback injection",
    )
    executor = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    target_has_stencil = braced_body(
        executor,
        "bool VK_Exec_ActiveTargetHasStencil(",
        "active render-target stencil query",
    )
    require_compact(
        target_has_stencil,
        """return vkExec.frameOpen
            && vkExec.activeDepthAttachmentView != VK_NULL_HANDLE
            && vkExec.activePipelineTarget.stencilFormat != VK_FORMAT_UNDEFINED;""",
        "active render-target stencil query",
    )

    interactions = read("src/renderer/Vulkan/vk_Interactions.cpp")
    require(
        interactions,
        "!r_vkShadowFallbackTest.GetBool()",
        "forced missing-stencil fallback coverage",
    )
    draw_lights = braced_body(
        interactions,
        "void VK_Interactions_DrawLights(",
        "Vulkan light interactions",
    )
    require_order(
        draw_lights,
        (
            "const bool activeTargetHasStencil = VK_Exec_ActiveTargetHasStencil();",
            """interPass.pipelineStencilShadow = activeTargetHasStencil
                ? VK_Exec_StencilShadowPipeline() : VK_NULL_HANDLE;""",
            """const int incompleteMapMask =
                vLight->shadowMapIncompleteMapMask
                | vLight->shadowMapPrelightMapMissingMask;""",
            """const int incompleteStencilMask =
                vLight->shadowMapIncompleteStencilMask
                | ( vLight->shadowMapPrelightStencilRequiredMask
                    & ~vLight->shadowMapPrelightStencilReadyMask );""",
            "const bool localReceiverNeedsFallback =",
            "const bool globalOpaqueReceiverNeedsFallback =",
            "const bool translucentReceiverNeedsFallback =",
            "const bool globalReceiverNeedsFallback =",
            "const bool missingRequiredShadow =",
            "const bool localStencilOwnershipComplete =",
            "const bool globalStencilOwnershipComplete =",
            "const bool localEmptyFallback =",
            "const bool globalEmptyFallback =",
            "const bool localStencilFallback =",
            "const bool globalStencilFallback =",
            "const bool anyStencilFallback =",
            "if ( missingRequiredShadow && unresolvedBeforeSubmit )",
            "if ( anyStencilFallback )",
            """const drawSurf_t *localGlobalVolumes =
                    localMapNeedsSupplement
                        ? vLight->globalShadowMapStencilSupplements
                        : vLight->globalShadows;""",
            "VK_StencilShadowPass( localGlobalVolumes );",
            "VK_DrawInteractionChain( vLight->localInteractions );",
            """const drawSurf_t *globalGlobalVolumes =
                    ( globalOpaqueMapNeedsSupplement ||
                        translucentMapNeedsSupplement )
                        ? vLight->globalShadowMapStencilSupplements
                        : vLight->globalShadows;""",
            """const drawSurf_t *globalLocalVolumes =
                    ( globalOpaqueMapNeedsSupplement ||
                        translucentMapNeedsSupplement )
                        ? vLight->localShadowMapStencilSupplements
                        : vLight->localShadows;""",
            "VK_StencilShadowPass( globalGlobalVolumes );",
            "VK_StencilShadowPass( globalLocalVolumes );",
            "VK_DrawInteractionChain( vLight->globalInteractions );",
            "if ( !localReceiverDrawn && vLight->localInteractions != NULL )",
            "if ( !globalOpaqueReceiverDrawn && vLight->globalInteractions != NULL )",
            "const bool translucentUsesStencilFallback =",
            "const bool translucentUsesEmptyFallback =",
            "const bool translucentUsesUnshadowedFallback =",
            "const bool drawTranslucentReceiver =",
            "if ( drawTranslucentReceiver )",
            "VK_DrawInteractionChain( vLight->translucentInteractions );",
        ),
        "fail-closed receiver and stencil fallback gates",
    )
    for forbidden in ("requiredStencilMask", "stencilShadowLight"):
        if forbidden in draw_lights:
            raise AssertionError(
                f"Vulkan fallback must remain per receiver ownership, not whole-light state: {forbidden}"
            )
    for snippet, label in (
        (
            """const bool stencilResourcesReady =
                activeTargetHasStencil &&
				interPass.pipelineStencilShadow != VK_NULL_HANDLE &&
				!r_vkShadowFallbackTest.GetBool();""",
            "stencil target and pipeline readiness",
        ),
        (
            """const bool localEmptyFallback =
                localReceiverNeedsFallback &&
                localStencilOwnershipComplete &&
                vLight->globalShadows == NULL;""",
            "LOCAL complete-empty fallback",
        ),
        (
            """const bool globalEmptyFallback =
                globalReceiverNeedsFallback &&
                globalStencilOwnershipComplete &&
                vLight->globalShadows == NULL &&
                vLight->localShadows == NULL;""",
            "GLOBAL complete-empty fallback",
        ),
        (
            """const bool localStencilFallback =
                localReceiverNeedsStencil &&
                !localEmptyFallback &&
                localStencilOwnershipComplete &&
                stencilResourcesReady;""",
            "LOCAL nonempty stencil fallback",
        ),
        (
            """const bool globalStencilFallback =
                globalReceiverNeedsStencil &&
                !globalEmptyFallback &&
                globalStencilOwnershipComplete &&
                stencilResourcesReady;""",
            "GLOBAL nonempty stencil fallback",
        ),
        (
            """globalStencilPassComplete =
                globalVolumePassComplete &&
                localVolumePassComplete;""",
            "GLOBAL two-pass runtime completion",
        ),
        (
            """const bool runtimeMissingRequiredShadow =
                ( localStencilFallback &&
                    !localReceiverDrewWithStencil )
                || ( globalStencilFallback &&
                    !globalStencilPassComplete );""",
            "late stencil submission degradation gate",
        ),
    ):
        require_compact(draw_lights, snippet, label)
    require(
        draw_lights,
        "stencil shadow volume submission incomplete; affected light receivers fall back unshadowed",
        "late stencil submission diagnostic",
    )
    expected_draw_counts = {
        "VK_DrawInteractionChain( vLight->localInteractions );": 4,
        "VK_DrawInteractionChain( vLight->globalInteractions );": 4,
        "VK_DrawInteractionChain( vLight->translucentInteractions );": 1,
    }
    for call, expected_count in expected_draw_counts.items():
        actual_count = draw_lights.count(call)
        if actual_count != expected_count:
            raise AssertionError(
                f"Per-ownership fallback draw cardinality changed for {call!r}: "
                f"{actual_count} != {expected_count}"
            )
    require_compact(
        draw_lights,
        """const bool translucentReceiverNeedsShadow = shadowingEnabled
            && vLight->translucentInteractions != NULL
            && ( hasGlobalCasters || hasLocalCasters
                || ( incompleteMapMask
                    & SHADOWMAP_RECEIVER_MASK_GLOBAL ) != 0 )
            && ( globalShadowState != NULL
                ? r_shadowMapTranslucentReceivers.GetBool()
                : r_stencilTranslucentShadows.GetBool() );""",
        "GLOBAL-owned translucent fallback selection",
    )
    require(
        draw_lights,
        "required shadow resource unavailable; affected light receivers fall back unshadowed",
        "unshadowed shadow-degradation diagnostic",
    )

    shared = read("src/renderer/tr_local.h")
    for token in (
        "SHADOWMAP_RECEIVER_MASK_LOCAL = 1 << 0",
        "SHADOWMAP_RECEIVER_MASK_GLOBAL = 1 << 1",
        "shadowMapIncompleteMapMask",
        "shadowMapIncompleteStencilMask",
        "shadowMapHybridIncompleteMask",
        "globalShadowMapStencilSupplements",
        "localShadowMapStencilSupplements",
        "shadowMapPrelightMapMissingMask",
        "shadowMapPrelightStencilRequiredMask",
        "shadowMapPrelightStencilReadyMask",
    ):
        require(shared, token, "shared ownership-completeness state")

    interaction_header = read("src/renderer/Interaction.h")
    require_order(
        interaction_header,
        (
            "bool shadowStencilEligible;",
            "bool shadowStencilUsesPrelight;",
        ),
        "cached stencil-fallback provenance",
    )

    frontend = read("src/renderer/Interaction.cpp")
    interaction_domain = read("src/renderer/ClassicInteractionDomain.cpp")
    require_compact(
        frontend,
        """hash = R_ShadowMapHashInt( hash, entityDef->lastModifiedFrameNum );
        hash = R_ShadowMapHashInt( hash, entityDef->dynamicModelFrameCount );""",
        "settled dynamic-model caster signature revision",
    )
    moments_support = braced_body(
        frontend,
        "static bool R_TranslucentShadowMapMomentsSupportedForLight(",
        "translucent shadow moment backend gate",
    )
    require_order(
        moments_support,
        (
            'cvarSystem->GetCVarString( "r_actualRenderApi" )',
            'idStr::Icmp( activeRenderApi, "vulkan" ) == 0',
            "return false;",
            "r_shadowMapTranslucentMoments.GetBool()",
        ),
        "explicit Vulkan translucent-moment rejection",
    )
    require_order(
        frontend,
        (
            "sint->shadowStencilEligible =",
            "sint->shadowStencilUsesPrelight =",
            "bool admittedShadowMapCaster = false;",
            "bool linkedShadowMapCaster = false;",
            """admittedShadowMapCaster =
                allowShadowMapCaster ||
                allowTranslucentShadowMapCaster;""",
            "linkedShadowMapCaster = true;",
            "vLight->shadowMapPrelightStencilRequiredMask |=",
            "vLight->shadowMapPrelightMapMissingMask |=",
            "} else if ( mapMissingCasterNeedsStencil )",
            "vLight->shadowMapIncompleteMapMask |=",
            "vLight->shadowMapIncompleteStencilMask |=",
            "mapMissingNeedsVisibleVolume = true;",
            "R_CullLocalBox(",
            "if ( mapMissingNeedsVisibleVolume )",
            "R_EnsureInteractionShadowCache(",
            "localShadowMapStencilSupplements",
            "globalShadowMapStencilSupplements",
        ),
        "front-end map/stencil ownership completeness",
    )
    require_compact(
        re.sub(r'//[^\n]*', '', frontend),
        """const bool mapMissingCasterNeedsStencil =
            shadowMapCasterPolicyActive &&
            !sint->shadowStencilUsesPrelight &&
            !linkedShadowMapCaster &&
            ( admittedShadowMapCaster ||
                ( sint->shadowStencilEligible &&
                    ( !sint->pointEmitterCasterSkip || shadowTris != NULL ) ) );""",
        "actual-caster map completeness provenance",
    )
    require_compact(
        frontend,
        """const bool forcePointEmitterStencilGeneration =
            pointMapPolicyActive &&
            sint->shadowStencilEligible &&
            R_ShouldSkipPointLightEmitterCaster( shader, tri,
                shadowMapLocalLightOrigin, lightDef->parms.lightRadius );""",
        "point-emitter stencil generation probe",
    )
    require_compact(
        interaction_domain,
        """( geometry.skinningMode != GEOMETRY_SKINNING_NONE
            && geometry.skinningMode != GEOMETRY_SKINNING_CPU )""",
        "CPU-skinned shared interaction geometry admission",
    )
    require_compact(
        frontend,
        """const bool suppressDynamicShadowVolume =
            surfaceCanCastStencilShadowVolume && shadowLODAdmitted &&
            ( surfaceCanCastDedicatedShadowMap ||
                surfaceCanCastTranslucentShadowMap ) &&
            model->IsDynamicModel() != DM_STATIC &&
            !forcePointEmitterStencilGeneration &&
            R_ShadowMapLightWillUseShadowMaps( lightDef );""",
        "point-emitter volume probe before dynamic elision",
    )
    per_surface_volume_gate = braced_body(
        frontend,
        "bool R_ShadowMapsNeedPerSurfaceStencilVolumes(",
        "backend-neutral mapped per-surface stencil-volume policy",
    )
    require_order(
        per_surface_volume_gate,
        (
            "!r_shadows.GetBool()",
            "!r_useShadowMap.GetBool()",
            "lightDef->parms.pointLight",
            "!r_shadowMapPointLights.GetBool()",
            "return false;",
            "return true;",
        ),
        "backend-neutral mapped per-surface stencil-volume policy",
    )

    require_compact(
        gl_backend,
        """static pointShadowMapCacheEntry_t *RB_ShadowMapAllocPointCacheEntry(
        const bool allowEviction = true )""",
        "GL point shadow cache non-evicting transaction allocator",
    )
    require_compact(
        gl_backend,
        """if ( !allowEviction ) {
        return NULL;
    }
    oldest->valid = false;""",
        "GL point shadow cache transaction spill",
    )
    require_compact(
        gl_backend,
        """schedule.pointEntry = RB_ShadowMapAllocPointCacheEntry( false );""",
        "GL sealed point shadow transaction cache preservation",
    )
    require_compact(
        gl_backend,
        """schedule.projectedEntry = RB_ShadowMapAllocProjectedCacheEntry(
            pass.projected.state.atlasDiv, false );""",
        "GL sealed projected shadow transaction cache preservation",
    )
    if "r_actualRenderApi" in per_surface_volume_gate:
        raise AssertionError(
            "Mapped per-surface prelight retention must not be Vulkan-only"
        )
    require_compact(
        frontend,
        """sint->shadowStencilUsesPrelight =
            sint->shadowStencilEligible &&
            R_LightHasRealPrelightModel( lightDef->parms ) &&
            model->IsStaticWorldModel() &&
            r_useOptimizedShadows.GetBool() &&
            !R_ShadowMapsNeedPerSurfaceStencilVolumes( lightDef );""",
        "mapped backend-neutral optimized-prelight split",
    )
    render_system = read("src/renderer/RenderSystem.cpp")
    require_order(
        render_system,
        (
            "if ( r_shadows.IsModified()",
            "|| r_useShadowMap.IsModified()",
            "|| r_useOptimizedShadows.IsModified()",
            "|| r_lod_shadows_percent.IsModified()",
            "|| r_shadowMapPointLights.IsModified()",
            "|| r_shadowMapConservativeCasters.IsModified()",
            "r_shadows.ClearModified();",
            "r_useShadowMap.ClearModified();",
            "r_useOptimizedShadows.ClearModified();",
            "r_lod_shadows_percent.ClearModified();",
            "r_shadowMapPointLights.ClearModified();",
            "r_shadowMapConservativeCasters.ClearModified();",
            "primaryWorld->FreeInteractions();",
        ),
        "live shadow representation rebuild",
    )
    require_compact(
        frontend,
        """const bool volumeElidedForShadowMaps =
            shadowTris != NULL &&
            !shadowMapCasterOnly &&
            R_ShadowMapLightWillUseShadowMaps( lightDef );""",
        "caster-only fallback-volume retention",
    )
    require_compact(
        frontend,
        """if ( !shadowMapCasterOnly &&
                r_useShadowCulling.GetBool() &&
                !R_ShouldDisableEntityCullingForLevelshot() &&
                !shadowTris->bounds.IsCleared() )""",
        "caster-only conservative full-light volume culling",
    )
    require_order(
        frontend,
        (
            "const bool linkedFullShadowVolume = R_LinkLightSurf(",
            "!linkedFullShadowVolume )",
            "const bool linkedStencilSupplement = R_LinkLightSurf(",
            "if ( !linkedStencilSupplement )",
            "vLight->shadowMapHybridIncompleteMask |=",
        ),
        "successful full and supplement volume linking",
    )

    prelight_source = read("src/renderer/tr_light.cpp")
    prelight_start = prelight_source.find(
        "static void R_AddOptimizedPrelightShadows("
    )
    prelight_end = prelight_source.find(
        "\n/*\n=================\nR_AddLightSurfaces", prelight_start
    )
    if prelight_start < 0 or prelight_end < 0:
        raise AssertionError(
            "Could not isolate optimized-prelight completeness resolution"
        )
    prelight = prelight_source[prelight_start:prelight_end]
    if "ShadowMapStencilSupplements" in prelight:
        raise AssertionError(
            "Combined optimized prelight volumes must remain full-fallback-only"
        )
    require_order(
        prelight,
        (
            "R_ShadowMapsNeedPerSurfaceStencilVolumes(",
            "return;",
            "idRenderModel *prelightModel = R_ViewLightPrelightModel( vLight );",
        ),
        "mapped Vulkan combined-prelight bypass",
    )
    require_order(
        prelight,
        (
            "vLight->shadowMapPrelightStencilRequiredMask = 0;",
            "vLight->shadowMapPrelightMapMissingMask = 0;",
            "const bool linkedPrelightVolume = R_LinkLightSurf(",
            "vLight->shadowMapIncompleteMapMask |=",
            "if ( linkedPrelightVolume )",
            "vLight->shadowMapPrelightStencilReadyMask |=",
        ),
        "optimized-prelight completeness resolution",
    )

    map_admission = braced_body(
        prelight_source,
        "bool R_ShadowMapLightWillUseShadowMaps(",
        "front-end shadow-map stencil-elision admission",
    )
    require_compact(
        map_admission,
        """if ( r_useOptimizedShadows.GetBool()
                && R_LightHasRealPrelightModel( lightDef->parms ) ) {
            return false;
        }""",
        "optimized-prelight complete stencil fallback retention",
    )
    if "protectStaticWorldNoSelfReceivers" in prelight:
        raise AssertionError(
            "Combined optimized prelights cannot conditionally lose LOCAL receiver ownership"
        )
    require_compact(
        prelight,
        """const bool linkedPrelightVolume = R_LinkLightSurf(
            &vLight->globalShadows,
            tri, NULL, light, NULL, vLight->scissorRect, true /* FIXME? */ );""",
        "optimized-prelight retail global-shadow routing",
    )
    require_compact(
        prelight,
        """vLight->shadowMapPrelightStencilReadyMask |=
            vLight->shadowMapPrelightStencilRequiredMask &
            ( SHADOWMAP_RECEIVER_MASK_LOCAL |
                SHADOWMAP_RECEIVER_MASK_GLOBAL );""",
        "optimized-prelight complete receiver readiness",
    )

    shadow_map = read("src/renderer/Vulkan/vk_ShadowMap.cpp")
    map_complete = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_MapOrHybridOwnershipComplete(",
        "mapped or hybrid ownership completeness gate",
    )
    require_order(
        map_complete,
        (
            "VK_ShadowMap_ReceiverMask( receiverPass )",
            "vLight->shadowMapIncompleteMapMask",
            "vLight->shadowMapPrelightMapMissingMask",
            "if ( ( incompleteMapMask & receiverMask ) == 0 )",
            "vLight->shadowMapHybridIncompleteMask",
            "vLight->shadowMapPrelightMapMissingMask",
            "vLight->globalShadowMapStencilSupplements != NULL",
            "vLight->localShadowMapStencilSupplements != NULL",
        ),
        "mapped or hybrid ownership completeness gate",
    )
    if shadow_map.count("!VK_ShadowMap_MapOrHybridOwnershipComplete(") != 2:
        raise AssertionError(
            "Point and projected receiver scheduling must both gate incomplete map/hybrid ownership"
        )

    abandon = braced_body(
        shadow_map,
        "void VK_ShadowMap_AbandonPreparedLights(",
        "prepared-shadow abandonment",
    )
    release_prepared = braced_body(
        shadow_map,
        "static void VK_ShadowMap_ReleasePreparedLights(",
        "prepared-shadow release transaction",
    )
    require_compact(
        abandon,
        "VK_ShadowMap_ReleasePreparedLights( true );",
        "prepared-shadow abandonment",
    )
    require_order(
        release_prepared,
        (
            "if ( markSticky && vkShadow.lights[ i ].valid )",
            "VK_ShadowMap_MarkStencilFallbackSticky(",
            "vkShadow.lights[ i ].passes[ passIndex ]",
            "vkShadow.lights[ i ].valid = false;",
        ),
        "prepared-shadow release transaction",
    )

    render = braced_body(
        shadow_map,
        "bool VK_ShadowMap_RenderAtlas(",
        "shadow-map caster rendering",
    )
    require_order(
        render,
        (
            "const bool resumedMainRendering = VK_Exec_BeginMainRendering( false );",
            "if ( !resumedMainRendering )",
            "VK_ShadowMap_AbandonPreparedLights();",
            "return resumedMainRendering;",
        ),
        "main-rendering resume failure",
    )


def validate_dynamic_caster_composition_contract() -> None:
    """GL SHADOWMAP_RENDER_STATIC_ONLY / _COMPOSE_DYNAMIC parity.

    A projected light with a moving caster must keep its resident entry,
    which holds STATIC depth only, and draw the dynamic chains over the
    published or restored tile. Re-rendering the whole static scene every
    frame is the behaviour this replaces, so the pieces that make the split
    safe are pinned here rather than left to reading.
    """
    shadow_map = read("src/renderer/Vulkan/vk_ShadowMap.cpp")
    header = read("src/renderer/Vulkan/vk_ShadowMap.h")

    require_compact(
        header,
        "bool composeDynamic;",
        "projected compose flag on the ownership pass state",
    )

    cacheable = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_StaticCacheable(",
        "static cache admission",
    )
    # Only the point path bakes dynamics into cached content. Projected
    # dynamics compose, so they must not defeat or invalidate the cache --
    # except while the opt-in shared stream can own the view, whose sealed
    # allowCacheReuse forbids a composed hit on both backends.
    require_compact(
        cacheable,
        """const bool dynamicsDefeatCache = haveDynamicCasters
            && ( pointLight
                || r_rendererSharedWorldInteraction.GetBool() );""",
        "projected dynamics keep the cache, point dynamics defeat it",
    )
    require_compact(
        cacheable,
        """if ( dynamicsDefeatCache ) {
            VK_ShadowMap_InvalidateLightCaches( renderWorld, lightIndex );
        }""",
        "only a cache-defeating class invalidates resident entries",
    )
    if compact(cacheable).count(compact("|| haveDynamicCasters")) != 0:
        raise AssertionError(
            "Projected dynamic casters must no longer reject static cacheability outright"
        )
    require_compact(
        cacheable,
        """if ( pointLight
                && tr.frameCount - history->lastDynamicFrame
                    < Max( 0,
                            r_shadowMapStaticHysteresisFrames.GetInteger() ) ) {""",
        "static hysteresis applies only to the baked point path",
    )

    allocate = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_AllocateProjectedPass(",
        "projected ownership allocation",
    )
    require_compact(
        allocate,
        """pass.composeDynamic = ( pass.cacheHit || pass.cacheUpdate )
            && VK_ShadowMap_PassHasDynamicCasters( light.vLight,
                    receiverPass );""",
        "cached projected passes owe their dynamic casters a compose draw",
    )

    dynamic_chains = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_PassHasDynamicCasters(",
        "ownership dynamic caster presence",
    )
    require_order(
        dynamic_chains,
        (
            "if ( vLight->globalShadowMapDynamicCasters != NULL )",
            "return true;",
            """return receiverPass == VK_SHADOW_RECEIVER_GLOBAL
                && vLight->localShadowMapDynamicCasters != NULL;""",
        ),
        "LOCAL maps see global dynamics, GLOBAL maps add local dynamics",
    )

    render = braced_body(
        shadow_map,
        "bool VK_ShadowMap_RenderAtlas(",
        "shadow-map caster rendering",
    )
    # An exact hit normally skips caster validation because the update that
    # published the signature proved it. A composed hit still draws live
    # casters, so it must prove the pipeline and representability again.
    require_compact(
        render,
        """} else if ( pass.composeDynamic
                && ( casterPipeline == VK_NULL_HANDLE
                    || !VK_ShadowMap_PassCastersRepresentable(
                            light, receiverPass ) ) ) {""",
        "composed cache hits revalidate their live dynamic casters",
    )
    require_order(
        render,
        (
            # publication copies the static-only tile out first ...
            "VK_ShadowMap_CopyDepthTile( cmd, vkShadow.atlasImage, pass.tileX, pass.tileY, cache.image, 0, 0, VK_ShadowMap_ProjectedBlockSize( light ) );",
            # ... then a resident hit restores its static tile ...
            "VK_ShadowMap_CopyDepthTile( cmd, cache.image, 0, 0, vkShadow.atlasImage, pass.tileX, pass.tileY, VK_ShadowMap_ProjectedBlockSize( light ) );",
            # ... and only then do the dynamics compose over both.
            "if ( projectedComposeCount > 0 && casterPipeline != VK_NULL_HANDLE )",
            "depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;",
            """VK_ShadowMap_DrawPassCasters( ctx, light, receiverPass, cascadeIndex,
                VK_SHADOW_CHAINS_DYNAMIC_ONLY )""",
            "vkCmdEndRendering( cmd );",
        ),
        "compose runs after publication and restore, never before",
    )
    if "VK_ATTACHMENT_LOAD_OP_LOAD" not in render:
        raise AssertionError(
            "The compose scope must preserve the cached tile instead of clearing it"
        )


def validate_csm_static_cache_contract() -> None:
    """r_shadowMapCacheCSM parity.

    A cached CSM light's resident content is the whole contiguous cascade
    block, not one tile. Copying or matching a single tile would restore a
    quarter of the cascades and leave the rest holding whatever the atlas
    row-scan last put there, so the physical block edge is part of both the
    slot's identity and every transfer.
    """
    shadow_map = read("src/renderer/Vulkan/vk_ShadowMap.cpp")

    block_size = braced_body(
        shadow_map,
        "static int VK_ShadowMap_ProjectedBlockSize(",
        "projected resident block edge",
    )
    require_compact(
        block_size,
        """return light.tileSize
            * idMath::ClampInt( 1, 2, light.projectedState.atlasDiv );""",
        "block edge is tileSize * atlasDiv",
    )

    require_compact(
        shadow_map,
        "int blockSize;",
        "resident entries record their physical block edge",
    )

    ensure_image = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_EnsureProjectedCacheImage(",
        "projected cache image creation",
    )
    require_order(
        ensure_image,
        (
            # a slot reused at a different block edge retires its image first
            "if ( entry.image != VK_NULL_HANDLE && entry.blockSize != blockSize )",
            "vkDeviceWaitIdle( vkCtx.device );",
            "vmaDestroyImage( vkCtx.allocator, entry.image, entry.allocation );",
            "VK_ShadowMap_ClearProjectedEntryMetadata( entry );",
            "ici.extent.width = (uint32_t)blockSize;",
            "ici.extent.height = (uint32_t)blockSize;",
            "entry.blockSize = blockSize;",
        ),
        "block-sized resident images retire on a size change",
    )

    find_entry = braced_body(
        shadow_map,
        "static int VK_ShadowMap_FindProjectedCacheEntry(",
        "projected exact resident lookup",
    )
    require_compact(
        find_entry,
        "&& entry.blockSize == blockSize",
        "an exact hit must match the physical block edge",
    )

    schedule = braced_body(
        shadow_map,
        "static vkShadowSchedule_t VK_ShadowMap_SchedulePass(",
        "projected cache scheduling",
    )
    require_compact(
        schedule,
        """const int projectedBlockSize = resourceSize
            * idMath::ClampInt( 1, 2, atlasDiv );""",
        "scheduling derives the cascade block edge",
    )
    for threaded in (
        "schedule.signature, resourceSize, projectedBlockSize );",
        "VK_ShadowMap_AllocProjectedCacheEntry( resourceSize, projectedBlockSize );",
    ):
        require_compact(
            schedule, threaded, "block edge reaches lookup and allocation"
        )

    render = braced_body(
        shadow_map,
        "bool VK_ShadowMap_RenderAtlas(",
        "cached shadow rendering",
    )
    require_compact(
        render,
        "|| cache->blockSize != VK_ShadowMap_ProjectedBlockSize( light )",
        "a resident hit revalidates the physical block edge",
    )
    finalize = braced_body(
        shadow_map,
        "static void VK_ShadowMap_FinalizeCachePasses(",
        "resident entry publication",
    )
    require_compact(
        finalize,
        "cache.blockSize = VK_ShadowMap_ProjectedBlockSize( light );",
        "publication records the physical block edge",
    )

    validate_pass = braced_body(
        shadow_map,
        "static bool VK_ClassicShadow_ValidatePhysicalPass(",
        "sealed-stream physical reconciliation",
    )
    require_compact(
        validate_pass,
        "&& cache->blockSize == VK_ShadowMap_ProjectedBlockSize( light )",
        "the sealed stream reconciles the same block edge",
    )


def validate_shadow_debug_mode_contract() -> None:
    """r_shadowMapDebugMode parity with glprogs/shadow_interaction.fs.

    The visualizations are only useful if they describe the sample the lit
    path actually took and if mode 0 costs nothing, so the selector reaches
    both receiver classes through the shadow ABI, the views run after
    sampling, and every debug branch is gated behind an explicit mode test.
    """
    projected = read("src/renderer/Vulkan/shaders/interaction_shadow.frag")
    point = read("src/renderer/Vulkan/shaders/interaction_shadow_point.frag")
    interactions = read("src/renderer/Vulkan/vk_Interactions.cpp")
    shadow_map = read("src/renderer/Vulkan/vk_ShadowMap.cpp")

    # The selector rides the shadow block, not a pipeline variant.
    for source, context in (
        (projected, "projected receiver debug ABI"),
        (point, "point receiver debug ABI"),
    ):
        require_compact(
            source,
            "vec4 debugParams;",
            context,
        )
        require_compact(
            source,
            """bool ShadowDebugModeIs(float mode) {
                return abs(shadow.debugParams.x - mode) < 0.5;
            }""",
            context,
        )

    # std140 mirrors must grow with the block or the shader reads garbage.
    require_compact(
        interactions,
        "float debugParams[ 4 ];",
        "std140 debug selector mirrors",
    )
    require_compact(
        interactions,
        """static_assert( sizeof( vkShadowBlock_t ) == 480,
            "projected shadow std140 block must remain 30 vec4s" );""",
        "projected block size follows the debug selector",
    )
    require_compact(
        interactions,
        """static_assert( sizeof( vkPointShadowBlock_t ) == 128,
            "point shadow std140 block must remain 8 vec4s" );""",
        "point block size follows the debug selector",
    )

    # Every block writer uploads it: the sealed stream and the legacy walker,
    # projected and point. A writer that forgot would silently show mode 0.
    if interactions.count("debugParams[ 0 ] = VK_ShadowMap_DebugModeValue();") != 4:
        raise AssertionError(
            "All four shadow block writers must upload the debug selector"
        )
    require_compact(
        interactions,
        """static float VK_ShadowMap_DebugModeValue( void ) {
            return (float)idMath::ClampInt( 0, SHADOWMAP_DEBUGMODE_COUNT - 1,
                    r_shadowMapDebugMode.GetInteger() );
        }""",
        "one clamped source for the uploaded debug selector",
    )

    # Mode 10 is a caster-side view: it belongs to the shadow pass, not the
    # receiver, exactly as RB_ShadowMapPolygonFactor/-Offset do it.
    require_compact(
        shadow_map,
        """const bool casterOffsetOff = idMath::ClampInt( 0,
            SHADOWMAP_DEBUGMODE_COUNT - 1,
            r_shadowMapDebugMode.GetInteger() )
                == SHADOWMAP_DEBUGMODE_CASTER_OFFSET_OFF;""",
        "caster-offset debug view",
    )
    require_compact(
        shadow_map,
        """ctx.slopeFactor = casterOffsetOff
            ? 0.0f : r_shadowMapPolygonFactor.GetFloat();""",
        "caster-offset debug view zeroes the slope term",
    )

    # The visualizations must run AFTER the sample, so the cascade they
    # describe is the one that was shaded.
    projected_main = braced_body(projected, "void main(", "projected receiver main")
    require_order(
        projected_main,
        (
            "SampleShadowFactor()",
            "if (ShadowVisualDebugMode())",
            "outColor = ShadowDebugOutput();",
            "light *= SampleShadowFactor();",
            "if (ShadowVisualDebugMode())",
            "outColor = ShadowDebugOutput();",
        ),
        "projected views replace the lit output after sampling",
    )
    point_main = braced_body(point, "void main(", "point receiver main")
    require_order(
        point_main,
        (
            "SampleShadowFactor()",
            "if (ShadowVisualDebugMode())",
            "outColor = PointShadowDebugOutput();",
            "light *= SampleShadowFactor();",
            "if (ShadowVisualDebugMode())",
            "outColor = PointShadowDebugOutput();",
        ),
        "point views replace the lit output after sampling",
    )

    # The selected cascade and its split blend are recorded during sampling.
    sample_factor = braced_body(
        projected, "float SampleShadowFactor(", "projected cascade selection"
    )
    require_order(
        sample_factor,
        (
            "gShadowCascadeIndex = cascadeIndex;",
            "gShadowCascadeBlend = 0.0;",
            "gShadowCascadeBlend = blend;",
        ),
        "the views describe the shaded cascade",
    )

    # Coordinate rejection is classified once, shared by sampling and the
    # invalid-mask view, so the two cannot disagree.
    project_coord = braced_body(
        projected, "bool ProjectShadowCoord(", "projected coordinate rejection"
    )
    require_order(
        project_coord,
        (
            "gShadowDebugState = max(gShadowDebugState, 1.0);",
            "gShadowDebugState = max(gShadowDebugState, 2.0);",
            "return true;",
        ),
        "rejection classes recorded for the invalid-mask view",
    )
    sample_cascade = braced_body(
        projected, "float SampleShadowCascade(", "projected cascade sampling"
    )
    require_compact(
        sample_cascade,
        "if (!ProjectShadowCoord(shadowCoord, localUv, depth))",
        "sampling and the views project through one helper",
    )

    # The suppression views alter the sample instead of replacing the output.
    receiver_bias = braced_body(
        projected, "float ShadowReceiverBias(", "projected receiver bias"
    )
    require_compact(
        receiver_bias,
        """if (ShadowDebugModeIs(kShadowDebugBiasOff)) {
            return 0.0;
        }""",
        "bias-off view",
    )
    require_compact(
        receiver_bias,
        """float normalBias = ShadowDebugModeIs(kShadowDebugReceiverPlaneBiasOff)
            ? 0.0 : shadow.biasParams.y;""",
        "receiver-plane-bias-off view",
    )
    point_bias = braced_body(
        point, "float ShadowReceiverBias(", "point receiver bias"
    )
    require_compact(
        point_bias,
        """if (ShadowDebugModeIs(kShadowDebugBiasOff)) {
            return 0.0;
        }""",
        "point bias-off view",
    )

    # Mode 0 must not pay for any of this: every debug read is behind a test.
    for source, context in (
        (projected, "projected receiver"),
        (point, "point receiver"),
    ):
        for guarded in ("ShadowDebugModeIs(", "shadow.debugParams.x"):
            if guarded not in source:
                raise AssertionError(
                    f"{context} lost its guarded debug selector {guarded!r}"
                )


def validate_update_admission_contract() -> None:
    """RB_ShadowMapBuildUpdateAdmissions parity.

    A limited r_shadowMapMaxUpdatesPerView spent in view-light-list order lets
    an off-screen light consume the budget a large, stale, on-screen one
    needed. Score first, then admit greedily. The estimate runs before the
    scheduling walk, so it must not reserve a slot, age a history, or
    invalidate an entry -- the walk would then see state it did not create.
    """
    shadow_map = read("src/renderer/Vulkan/vk_ShadowMap.cpp")

    build = braced_body(
        shadow_map,
        "static void VK_ShadowMap_BuildUpdateAdmissions(",
        "importance-ordered update admission",
    )
    require_order(
        build,
        (
            # an unlimited budget or a policy that already forbids fresh
            # renders needs no ordering at all
            "const int updateBudget = r_shadowMapMaxUpdatesPerView.GetInteger();",
            "if ( updateBudget <= 0",
            "r_shadowMapSubviewPolicy.GetInteger() ) > 0 )",
            "VK_ShadowMap_EstimateUpdateCost( vLight, viewDef, cost, staleness )",
            # GL's score: screen coverage, staleness, camera inside the light
            """candidates[ candidateCount ].score = scissorArea / 32
                + staleness * 512
                + ( vLight->viewInsideLight ? 8192 : 0 );""",
            # everything fits: first-come order is already correct
            "if ( candidateCount == 0 || totalCost <= updateBudget )",
            "return;",
            # (score desc, lightIndex asc), then greedy admission
            """while ( j >= 0
                && ( candidates[ j ].score < key.score
                    || ( candidates[ j ].score == key.score
                        && candidates[ j ].lightIndex
                            > key.lightIndex ) ) )""",
            "int remaining = updateBudget;",
            "if ( candidates[ i ].cost > remaining && vkShadowAdmittedLightCount > 0 )",
            "continue;",
            "remaining = Max( 0, remaining - candidates[ i ].cost );",
            "vkShadowAdmissionsActive = true;",
        ),
        "importance-ordered update admission",
    )

    # The estimate must be side-effect free. FindProjectedCacheEntry and
    # FindPointCacheEntry reserve; StaticCacheable ages history and can
    # invalidate. The read-only peers exist for exactly that reason.
    estimate = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_EstimateUpdateCost(",
        "read-only update-cost estimate",
    )
    for mutating in (
        "VK_ShadowMap_FindProjectedCacheEntry(",
        "VK_ShadowMap_FindPointCacheEntry(",
        "VK_ShadowMap_AllocProjectedCacheEntry(",
        "VK_ShadowMap_AllocPointCacheEntry(",
        "VK_ShadowMap_StaticCacheable(",
        "VK_ShadowMap_InvalidateLightCaches(",
    ):
        if mutating in estimate:
            raise AssertionError(
                f"The admission estimate must not call the mutating {mutating!r}"
            )
    for read_only in (
        "VK_ShadowMap_StaticCacheableReadOnly(",
        "VK_ShadowMap_PeekPointCacheEntry(",
        "VK_ShadowMap_PeekProjectedCacheEntry(",
    ):
        require(estimate, read_only, "read-only admission estimate")

    history_const = braced_body(
        shadow_map,
        "static const vkShadowLightHistory_t *VK_ShadowMap_FindLightHistoryConst(",
        "pure light-history lookup",
    )
    for mutating in ("oldest", "= tr.frameCount", "history.valid = true"):
        if mutating in history_const:
            raise AssertionError(
                "The const history lookup must never claim or evict a slot"
            )

    # Staleness needs the frame the CONTENT was written, not the frame a view
    # last touched the slot; a cache hit refreshes the latter every frame.
    for entry_marker, context in (
        ("typedef struct vkProjectedShadowCacheEntry_s", "projected entry"),
        ("typedef struct vkPointShadowCacheEntry_s", "point entry"),
    ):
        entry = braced_body(shadow_map, entry_marker, context)
        require_compact(
            entry,
            "int lastUpdatedFrame;",
            f"{context} records its content write",
        )
    finalize = braced_body(
        shadow_map,
        "static void VK_ShadowMap_FinalizeCachePasses(",
        "resident entry publication",
    )
    if finalize.count("lastUpdatedFrame = tr.frameCount;") != 2:
        raise AssertionError(
            "Both resident classes must stamp their content-write frame on publication"
        )

    # Ordering decides WHICH lights spend a limited budget; the running count
    # still decides when it is gone, and a required map bypasses both.
    schedule = braced_body(
        shadow_map,
        "static vkShadowSchedule_t VK_ShadowMap_SchedulePass(",
        "budget admission gate",
    )
    require_compact(
        schedule,
        """const bool admissionDenied = updateBudget > 0
            && vkShadowAdmissionsActive
            && !VK_ShadowMap_UpdateAdmitted( lightIndex );""",
        "scheduling honours the admission list",
    )
    require_compact(
        schedule,
        """if ( updateBudget > 0
                && ( vkShadow.freshUpdates >= updateBudget || admissionDenied )
                && !mapRequiredForCorrectness ) {""",
        "a correctness-required map still bypasses the budget",
    )

    prepare = braced_body(
        shadow_map,
        "int VK_ShadowMap_PrepareViewLights(",
        "per-view admission build",
    )
    require_order(
        prepare,
        (
            "VK_ShadowMap_BeginCacheView( viewDef );",
            "VK_ShadowMap_BuildUpdateAdmissions( viewDef );",
        ),
        "admissions are built before any light can claim the budget",
    )


def validate_shadow_report_and_storage_honesty() -> None:
    """r_shadowMapReport depth, and cvars that cannot lie about Vulkan.

    A report that prints zeroes for work a backend never does reads as "fast"
    rather than "not implemented", so the two GL-only timing cvars and the
    GL-only point storage cvar announce themselves instead.
    """
    shadow_map = read("src/renderer/Vulkan/vk_ShadowMap.cpp")
    init = read("src/renderer/RenderSystem_init.cpp")

    report = braced_body(
        shadow_map,
        "static void VK_ShadowMap_ReportViewCache(",
        "per-view shadow report",
    )
    require_order(
        report,
        (
            "if ( r_shadowMapReport.GetInteger() < 1 )",
            "r_shadowMapReportInterval.GetInteger()",
            "vkShadow.admissionDenied,",
            "vkShadow.atlasTilesRendered,",
            "vkShadow.atlasTilesAllocated,",
            "vkShadow.pointFacesRendered,",
            "VK_ShadowMap_ResidentBytes() / ( 1024.0 * 1024.0 ),",
            "VK_ShadowMap_ReportViewLights();",
        ),
        "the view line reports what the view actually spent",
    )
    # The timing cvars now drive real measurements, but a device without
    # usable timestamps must say so rather than printing a convincing zero.
    require_compact(
        report,
        """if ( r_shadowMapGpuTimerQueries.GetBool()
            || r_shadowMapGpuSyncTimings.GetBool() ) {""",
        "the shadow timing cvars gate the timing line",
    )
    require_compact(
        report,
        "if ( !timing.available ) {",
        "a device without usable timestamps reports unavailable",
    )
    require_compact(
        report,
        "if ( r_shadowMapPointHighPrecision.GetBool() ) {",
        "GL-only point storage cvar announces itself",
    )

    per_light = braced_body(
        shadow_map,
        "static void VK_ShadowMap_ReportViewLights(",
        "per-light shadow decisions",
    )
    require_compact(
        per_light,
        "if ( r_shadowMapReport.GetInteger() < 2 )",
        "per-light decisions are report level 2",
    )
    # Each ownership names the decision it reached, including the two that
    # only exist once composition and aliasing do.
    for outcome in (
        '"unused"',
        '"unmapped"',
        '"alias"',
        '"reuse+compose"',
        '"reuse"',
        '"publish+compose"',
        '"publish"',
        '"scratch"',
    ):
        require(per_light, outcome, "per-light ownership outcome")

    resident = braced_body(
        shadow_map,
        "static double VK_ShadowMap_ResidentBytes(",
        "resident shadow memory",
    )
    require_order(
        resident,
        (
            "vkShadow.atlasImage != VK_NULL_HANDLE",
            "projected.image != VK_NULL_HANDLE",
            "vkShadow.pointCache[ i ].cube.image != VK_NULL_HANDLE",
            "vkShadow.pointCubes[ i ].image != VK_NULL_HANDLE",
        ),
        "every created shadow image counts toward resident memory",
    )

    # An inert cvar must not partition the cache: hashing it would discard
    # every resident point map on a toggle that changes nothing.
    signature = braced_body(
        shadow_map,
        "static int VK_ShadowMap_BuildPassSignatureForView(",
        "resident cache signature",
    )
    if "r_shadowMapPointHighPrecision.GetBool()" in signature:
        raise AssertionError(
            "Vulkan point cubes always store native depth, so the storage cvar"
            " must not partition their cache"
        )
    require(
        signature,
        "r_shadowMapPointDepthCompare.GetBool()",
        "the compare mode still partitions the point cache",
    )

    require(
        init,
        "OpenGL only: the Vulkan backend always stores native depth",
        "the point storage cvar documents its backend scope",
    )


def validate_shadow_gpu_timing_contract() -> None:
    """r_shadowMapGpuTimerQueries / r_shadowMapGpuSyncTimings on Vulkan.

    OpenGL brackets each shadow phase with glFinish. Vulkan cannot: the pass
    is being RECORDED, so there is nothing submitted to wait for. Timestamp
    spans measure it instead, and the synchronized mode is applied at readback.

    It lives in its own TU because renderer_temporal_presentation pins that
    VulkanGpuFrameTiming.cpp never waits at all -- a file-scoped guarantee
    that dynamic-resolution timing cannot block the frame.

    Two properties keep that safe. Resetting a query pair is illegal inside a
    dynamic-rendering scope, so every span opens outside one. And a wait on a
    span whose command buffer was never submitted would never return, so the
    wait is gated on an age past which the frame loop has already waited that
    slot's fence, with an age-out behind it.
    """
    timing = read("src/renderer/Vulkan/VulkanShadowTiming.cpp")
    header = read("src/renderer/Vulkan/VulkanShadowTiming.h")
    shadow_map = read("src/renderer/Vulkan/vk_ShadowMap.cpp")

    require_compact(
        header,
        """typedef enum vkShadowTimingPhase_e {
            VK_SHADOW_TIMING_MAP_RENDER = 0,""",
        "the phase split OpenGL reports",
    )

    begin_view = braced_body(
        timing,
        "void VK_ShadowGpuTiming_BeginView(",
        "per-view shadow timing resolve",
    )
    # A span from the current frame has not been submitted; waiting on it
    # would hang, so it is only ever carried.
    require_compact(
        begin_view,
        """if ( span.frameNumber >= tr.frameCount ) {
            vkShadowTimingReport.pending++;
            continue;
        }""",
        "spans recorded this frame are never resolved",
    )
    require_compact(
        begin_view,
        """VK_ShadowGpuTiming_ResolveSpan( span, i,
            synchronized && age > VK_FRAMES_IN_FLIGHT );""",
        "the synchronized wait is gated on a retired frame slot",
    )
    require_compact(
        begin_view,
        "if ( age > VK_SHADOW_TIMING_MAX_SPAN_AGE_FRAMES ) {",
        "a span whose submission never happened is dropped, not waited on",
    )
    # A span opened and never closed holds two queries forever.
    require_compact(
        begin_view,
        """if ( !span.open
                || tr.frameCount - span.frameNumber
                    > VK_FRAMES_IN_FLIGHT ) {""",
        "an unclosed span is reclaimed instead of leaking its queries",
    )

    resolve = braced_body(
        timing,
        "static void VK_ShadowGpuTiming_ResolveSpan(",
        "shadow timing readback",
    )
    require_compact(
        resolve,
        """if ( synchronized ) {
            flags |= VK_QUERY_RESULT_WAIT_BIT;
        }""",
        "synchronized mode waits rather than dropping",
    )
    require_compact(
        resolve,
        """if ( result == VK_NOT_READY ) {""",
        "an unready poll is carried, not counted as zero",
    )

    begin_phase = braced_body(
        timing,
        "void VK_ShadowGpuTiming_BeginPhase(",
        "shadow timing span open",
    )
    require_order(
        begin_phase,
        (
            "const int index = VK_ShadowGpuTiming_ClaimSpan( phase );",
            # ring pressure must degrade to a lost sample, never to a stall
            "if ( index < 0 )",
            "vkShadowTimingReport.dropped++;",
            "vkCmdResetQueryPool( commandBuffer, vkShadowTimingQueryPool, firstQuery, 2 );",
            "vkCmdWriteTimestamp( commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,",
        ),
        "a span resets its queries before writing its begin timestamp",
    )

    # Every span must open outside a dynamic-rendering scope, because
    # vkCmdResetQueryPool is illegal inside one. Check each open site is
    # followed by vkCmdBeginRendering rather than preceded by it.
    render = braced_body(
        shadow_map,
        "bool VK_ShadowMap_RenderAtlas(",
        "shadow pass timing spans",
    )
    compact_render = compact(render)
    for opened_before_scope in (
        # fresh projected scope
        """VK_ShadowGpuTiming_BeginPhase( cmd,
            VK_SHADOW_TIMING_MAP_RENDER );
            VK_ShadowMap_ImageBarrier( cmd, vkShadow.atlasImage, 1,""",
        # resident transfers
        """if ( haveCacheUpdates || haveCacheHits ) {
            VK_ShadowGpuTiming_BeginPhase( cmd,
                VK_SHADOW_TIMING_CACHE_REUSE );
        }""",
        # point cube faces, opened before the first per-face scope
        """VK_ShadowGpuTiming_BeginPhase( cmd, VK_SHADOW_TIMING_MAP_RENDER );
            vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pointCasterPipeline );""",
    ):
        require_compact(
            render,
            opened_before_scope,
            "spans open outside a dynamic-rendering scope",
        )
    # Compose is timed as MAP_RENDER, the same phase split OpenGL uses.
    if compact_render.count(
        compact("VK_ShadowGpuTiming_BeginPhase( cmd, VK_SHADOW_TIMING_MAP_RENDER )")
    ) != 3:
        raise AssertionError(
            "The fresh, compose and point regions must all be timed as MAP_RENDER"
        )
    if compact_render.count(
        compact("VK_ShadowGpuTiming_EndPhase( cmd, VK_SHADOW_TIMING_MAP_RENDER )")
    ) != 3:
        raise AssertionError( "Every MAP_RENDER span must be closed" )

    # A view whose main scope never resumes is never submitted.
    require_compact(
        render,
        "VK_ShadowGpuTiming_AbandonView();",
        "an unsubmitted view releases its spans",
    )
    require_compact(
        shadow_map,
        "VK_ShadowGpuTiming_Shutdown();",
        "the query pool retires with the shadow resources",
    )


def validate_shadow_debug_overlay_contract() -> None:
    """r_shadowMapDebugOverlay: the GL mini-map, sampled safely on Vulkan.

    The overlay reads live shadow resources from inside the scene's render
    scope, which is the only place in the module where a diagnostic touches
    the same images the receivers just sampled. Each pin below guards one way
    that could go wrong.
    """

    shadow_map = read("src/renderer/Vulkan/vk_ShadowMap.cpp")
    executor = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    header = read("src/renderer/Vulkan/vk_ShadowMap.h")

    require(
        header,
        "void\tVK_ShadowMap_DebugOverlayDraw( const viewDef_t *viewDef );",
        "the overlay is exported for the interaction walkers' caller",
    )

    # One call site, after BOTH walkers, so the overlay covers the shared
    # ownership path, the legacy walker, and the views where neither drew a
    # light (the GL "NO MAP" state). r_skipInteractions suppresses it exactly
    # like the GL pass it belongs to.
    if executor.count("VK_ShadowMap_DebugOverlayDraw(") != 1:
        raise AssertionError(
            "The Vulkan shadow overlay must have exactly one call site, so both"
            " interaction walkers and the light-less views reach it identically"
        )
    require_order(
        executor,
        (
            "VK_ClassicInteraction_DrawOwnedView( viewDef );",
            "VK_Interactions_DrawLights( viewDef );",
            "if ( !r_skipInteractions.GetBool() ) {",
            "VK_ShadowMap_DebugOverlayDraw( viewDef );",
        ),
        "the overlay draws after the interaction dispatch, under r_skipInteractions",
    )

    overlay = braced_body(
        shadow_map,
        "void VK_ShadowMap_DebugOverlayDraw( const viewDef_t *viewDef ) {",
        "Vulkan shadow debug overlay draw",
    )
    select = braced_body(
        shadow_map,
        "static bool VK_ShadowMap_OverlaySelect( const viewDef_t *viewDef,",
        "Vulkan shadow debug overlay selection",
    )

    require(
        overlay,
        "if ( !r_shadowMapDebugOverlay.GetBool() || viewDef == NULL ) {",
        "the overlay is gated on its own cvar",
    )

    # The shadow pass interrupts the dynamic-rendering scope. Appending draws
    # to a scope that failed to resume is invalid usage, not a cosmetic bug.
    require_order(
        overlay,
        (
            "if ( !VK_Exec_MainRenderingScopeOpen() ) {",
            "return;",
            "draw.cmd = VK_Exec_ActiveCmd();",
        ),
        "the overlay proves the render scope is recording before it records",
    )
    require(
        executor,
        "bool VK_Exec_MainRenderingScopeOpen( void ) {",
        "the executor exposes the recording state the overlay checks",
    )
    require_compact(
        executor,
        "return vkExec.frameOpen && vkExec.mainScopeOpen && vkExec.cmd != VK_NULL_HANDLE;",
        "the scope query answers for the frame AND the scope",
    )

    # A stale table is worse than no table: the panel would show another
    # view's light while claiming to describe this one. Shared ownership can
    # reach a view that prepared nothing at all (shadowMapPassCount 0).
    require(
        shadow_map,
        "const viewDef_t *\tpreparedView;",
        "the module records which view produced the light table",
    )
    prepare = braced_body(
        shadow_map,
        "int VK_ShadowMap_PrepareViewLights( const viewDef_t *viewDef,",
        "prepared-view stamp",
    )
    require_order(
        prepare,
        ("vkShadow.numLights = 0;", "vkShadow.preparedView = viewDef;",
         "vkShadow.preparedFrame = tr.frameCount;"),
        "preparation stamps the view that owns the table",
    )
    release = braced_body(
        shadow_map,
        "static void VK_ShadowMap_ReleasePreparedLights( const bool markSticky ) {",
        "prepared-view stamp release",
    )
    require_order(
        release,
        ("vkShadow.numLights = 0;", "vkShadow.preparedView = NULL;",
         "vkShadow.preparedFrame = -1;"),
        "releasing the table clears the view that owned it",
    )
    require_order(
        select,
        (
            "if ( !VK_ShadowMap_ViewPrepared( viewDef ) ) {",
            "return false;",
        ),
        "the overlay refuses a light table another view prepared",
    )
    identity = braced_body(shadow_map, "static bool VK_ShadowMap_ViewPrepared(", "frame-arena ownership")
    require_compact(identity, "return viewDef != NULL && vkShadow.preparedView == viewDef && vkShadow.preparedFrame == tr.frameCount;",
                    "reused frame-arena addresses cannot revive old light records")
    require(
        overlay,
        "const bool viewPrepared = VK_ShadowMap_ViewPrepared( viewDef );",
        "the readout knows whether the view counters describe this view",
    )
    for counter in (
        "vkShadow.projectedCacheHits",
        "vkShadow.pointCacheHits",
        "vkShadow.projectedFreshUpdates",
        "vkShadow.pointFreshUpdates",
        "vkShadow.composePasses",
        "vkShadow.atlasTilesRendered",
        "vkShadow.atlasTilesAllocated",
        "vkShadow.pointFacesRendered",
        "vkShadow.budgetFallbacks",
        "vkShadow.admissionDenied",
        "vkShadow.subviewFallbacks",
    ):
        require_compact(
            overlay,
            f"viewPrepared ? {counter} : 0",
            "every displayed view counter reports zero for a view that prepared nothing",
        )

    # Both shadow descriptor families declare DEPTH_STENCIL_READ_ONLY_OPTIMAL.
    # Sampling through a descriptor whose declared layout disagrees with the
    # image is undefined, and the overlay runs after a pass that legitimately
    # leaves images in transfer/attachment layouts on some paths.
    require_compact(
        select,
        "const bool atlasReadable = vkShadow.atlasImage != VK_NULL_HANDLE"
        " && vkShadow.atlasLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;",
        "the projected panel samples the atlas only in the layout its descriptor declares",
    )
    require_compact(
        select,
        "const VkDescriptorSet atlasSet = atlasReadable"
        " ? VK_Exec_ShadowDescriptorSet() : VK_NULL_HANDLE;",
        "an unreadable atlas yields no descriptor rather than a mismatched one",
    )
    require_compact(
        select,
        "if ( cube == NULL || cube->image == VK_NULL_HANDLE"
        " || cube->layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ) {",
        "the point panel samples a cube only in the layout its descriptor declares",
    )

    # The cube resolution must agree with the render walk's, or the panel
    # would show a different image than the receivers sampled.
    pass_cube = braced_body(
        shadow_map,
        "static const vkPointShadowCube_t *VK_ShadowMap_OverlayPassCube(",
        "overlay point cube resolution",
    )
    require_order(
        pass_cube,
        (
            "if ( pass.cacheEntry >= 0 && pass.cacheEntry < VK_SHADOW_MAX_CACHE_SLOTS ) {",
            "return &vkShadow.pointCache[ pass.cacheEntry ].cube;",
            "if ( pass.cubeIndex >= 0 && pass.cubeIndex < VK_SHADOW_MAX_POINT_CUBES ) {",
            "return &vkShadow.pointCubes[ pass.cubeIndex ];",
        ),
        "the overlay resolves a pass's cube the way the point render walk does",
    )

    # An aliased pass points at the other ownership's resource, which is
    # already reachable under the name of the pass that owns it.
    require_compact(
        select,
        "if ( !pass.valid || pass.resourcePass != (vkShadowReceiverPass_t)passIndex ) { continue; }",
        "only a receiver ownership that owns its own resource is shown",
    )
    require(
        select,
        "if ( singleLight >= 0 && lightDefIndex != singleLight ) {",
        "r_singleLight selects the displayed light exactly, as it does on OpenGL",
    )

    # The interaction pass leaves a negative-height viewport for GL winding
    # parity; reusing it would flip the panel and its text. Vulkan cannot
    # query the scissor the shared walker last latched, so the overlay
    # restores the view rect explicitly instead of leaving a per-primitive one.
    require_order(
        overlay,
        (
            "viewport.height = (float)fbHeight;",
            "vkCmdSetViewport( draw.cmd, 0, 1, &viewport );",
            "vkCmdSetScissor( draw.cmd, 0, 1, &scissor );",
            "VK_Exec_SetViewScissor( draw.cmd, viewDef, fbHeight );",
        ),
        "the overlay owns its viewport/scissor and restores the view scissor",
    )
    if "viewport.height = -" in overlay:
        raise AssertionError(
            "The overlay's own viewport must be positive-height: its vertex"
            " stage maps top-left pixels straight to Vulkan clip space"
        )
    require(
        executor,
        "void VK_Exec_SetViewScissor( VkCommandBuffer cmd, const viewDef_t *viewDef, int fbHeight ) {",
        "the executor exposes the view scissor the overlay restores",
    )

    # 2D semantics over the finished scene, exactly like the GL overlay's
    # GLS_DEPTHFUNC_ALWAYS + depth/stencil disable.
    for state in (
        "vkCmdSetDepthTestEnable( draw.cmd, VK_FALSE );",
        "vkCmdSetDepthWriteEnable( draw.cmd, VK_FALSE );",
        "vkCmdSetStencilTestEnable( draw.cmd, VK_FALSE );",
        "vkCmdSetCullMode( draw.cmd, VK_CULL_MODE_NONE );",
        "vkCmdSetDepthBiasEnable( draw.cmd, VK_FALSE );",
    ):
        require(overlay, state, "the overlay latches 2D dynamic state before drawing")

    # shadowSetLayout carries the receivers' dynamic shadow block at binding 1,
    # so binding the panel set without an offset is invalid even though the
    # panel shaders never read it.
    require_compact(
        overlay,
        "vkCmdBindDescriptorSets( draw.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,"
        " draw.layout, 0, 1, &selection.panelSet, 1, &dynamicOffset );",
        "the panel set binds with the dynamic offset its layout requires",
    )
    require_order(
        overlay,
        ("if ( drawPanel ) {", "vkCmdBindDescriptorSets( draw.cmd"),
        "no descriptor set is bound when there is no panel to sample",
    )

    # A diagnostic that moves backEnd.pc changes the numbers r_rendererMetrics
    # reports while it is on, and the GL overlay's immediate-mode quads never
    # reach those counters either. Every overlay draw is emitted by the quad
    # helper, so both it and the entry point have to stay silent.
    quad = braced_body(
        shadow_map,
        "static void VK_ShadowMap_OverlayQuad( vkShadowOverlayDraw_t &draw,",
        "overlay quad emitter",
    )
    require(quad, "vkCmdDraw( draw.cmd, 6, 1, 0, 0 );", "the overlay emits its quads here")
    for source, name in ( ( overlay, "entry point" ), ( quad, "quad emitter" ) ):
        if "VK_Device_Count" in source:
            raise AssertionError(
                f"The overlay {name} must not account its own draws: it would"
                " move the renderer counters it exists to help read"
            )

    validate_shadow_debug_overlay_pipelines(executor)
    validate_shadow_debug_overlay_shaders()


def validate_shadow_debug_overlay_pipelines(executor: str) -> None:
    """The overlay's three pipelines and the layout they share."""

    require(
        executor,
        "typedef struct vkShadowOverlayPush_s {",
        "the overlay declares its own push block",
    )
    require_compact(
        executor,
        "overlayPushRange.size = sizeof( vkShadowOverlayPush_t );",
        "the overlay layout advertises its own push range",
    )
    # 128 bytes is the guaranteed maxPushConstantsSize; the overlay block must
    # stay inside it on every implementation, not just the ones tested.
    push_block = braced_body(
        executor,
        "typedef struct vkShadowOverlayPush_s {",
        "overlay push block",
    )
    vec4s = push_block.count("[ 4 ];")
    if vec4s * 16 > 128:
        raise AssertionError(
            f"The overlay push block is {vec4s * 16} bytes; the guaranteed"
            " maxPushConstantsSize is 128"
        )

    variant = braced_body(
        executor,
        "static VkPipeline VK_Exec_ShadowOverlayPipelineVariant( vkSpecialPipelineKind_t kind,",
        "overlay pipeline variant",
    )
    require_compact(
        variant,
        "vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;",
        "the overlay pipelines declare a vertex input state",
    )
    if "pVertexBindingDescriptions" in variant or "pVertexAttributeDescriptions" in variant:
        raise AssertionError(
            "The overlay generates its quad from gl_VertexIndex; binding a"
            " vertex buffer would make a diagnostic compete for the frame rings"
        )
    require_compact(
        variant,
        "GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA",
        "the overlay blends over the scene like the GL overlay's GL_State",
    )
    require_compact(
        variant,
        "vkExec.shadowOverlayPipelineLayout, false, false, target",
        "the overlay pipelines keep the scene's color attachment",
    )

    # Three variants: the two panels differ only in the view type they declare
    # for the raw sampler, and the text variant needs no sampler at all so the
    # "NO MAP" readout can draw with nothing bound.
    for kind in (
        "VK_SPECIAL_SHADOW_OVERLAY_PANEL",
        "VK_SPECIAL_SHADOW_OVERLAY_POINT_PANEL",
        "VK_SPECIAL_SHADOW_OVERLAY_TEXT",
    ):
        require(executor, kind, "the overlay pipeline kinds are distinct cache keys")
    require_compact(
        executor,
        "VkPipeline VK_Exec_ShadowOverlayPanelPipeline( bool pointLight ) {"
        " return pointLight"
        " ? VK_Exec_ShadowOverlayPipelineVariant( VK_SPECIAL_SHADOW_OVERLAY_POINT_PANEL,"
        " vkExec.shadowOverlayPointFragModule )"
        " : VK_Exec_ShadowOverlayPipelineVariant( VK_SPECIAL_SHADOW_OVERLAY_PANEL,"
        " vkExec.shadowOverlayFragModule ); }",
        "the light class picks the panel variant whose sampler view type matches",
    )
    require_compact(
        executor,
        "plci.setLayoutCount = 1; plci.pSetLayouts = &vkExec.shadowSetLayout;"
        " plci.pPushConstantRanges = &overlayPushRange;",
        "the overlay layout reuses the shadow receiver set layout at slot 0",
    )


def validate_shadow_debug_overlay_shaders() -> None:
    """The three overlay shaders and their committed SPIR-V registration."""

    panel = read("src/renderer/Vulkan/shaders/shadow_debug_overlay.frag")
    point_panel = read("src/renderer/Vulkan/shaders/shadow_debug_overlay_point.frag")
    text = read("src/renderer/Vulkan/shaders/shadow_debug_overlay_text.frag")
    vertex = read("src/renderer/Vulkan/shaders/shadow_debug_overlay.vert")

    require(
        vertex,
        "kCorners[gl_VertexIndex]",
        "the overlay quad comes from the vertex index, not a vertex buffer",
    )
    # Vulkan clip space already puts y = -1 at the top row, so the GL vertex
    # program's flip must NOT be reproduced here.
    require(
        vertex,
        "vec2 clip = pixel / extent * 2.0 - 1.0;",
        "top-left pixels map straight to Vulkan clip space",
    )

    # Binding 2 is the raw (compare-disabled, NEAREST) sampler. Binding 0 is
    # the LEQUAL compare sampler, whose result is a shadow test, not the
    # stored depth the panel is supposed to show.
    require(
        panel,
        "layout(set = 0, binding = 2) uniform sampler2D shadowRawMap;",
        "the projected panel reads stored depth through the raw sampler",
    )
    require(
        point_panel,
        "layout(set = 0, binding = 2) uniform samplerCube shadowRawMap;",
        "the point panel reads stored depth through the raw cube sampler",
    )
    for source, name in (
        (panel, "projected panel"),
        (point_panel, "point panel"),
        (text, "text"),
    ):
        if "binding = 0" in source or "Shadow " in source:
            raise AssertionError(
                f"The overlay {name} shader must not sample the compare"
                " sampler: a shadow-test result is not the stored depth"
            )

    # The "NO MAP" readout has to draw when there is no shadow resource at
    # all, so its shader may not statically use a descriptor.
    if "uniform sampler" in text:
        raise AssertionError(
            "The overlay text shader must declare no sampler, or the 'NO MAP'"
            " state would need a descriptor set that does not exist"
        )
    require(
        text,
        "const uvec2 kFont[44]",
        "the 5x7 font lives in exactly one shader",
    )
    for source, name in ((panel, "projected panel"), (point_panel, "point panel")):
        if "kFont" in source:
            raise AssertionError(
                f"The overlay {name} shader must not duplicate the font: the"
                " text variant owns every glyph and solid fill"
            )

    # Vulkan point cubes always store native depth (r_shadowMapPointHighPrecision
    # is OpenGL-only), so the packed two-channel decode the GL overlay needs
    # must not be carried over.
    if "UnpackDepth" in point_panel or "1.0 / 255.0" in point_panel:
        raise AssertionError(
            "Vulkan point cubes store native depth; the overlay must not decode"
            " the OpenGL packed-color encoding"
        )

    # The cascade grid must line up with the panel's own edges wherever in the
    # atlas the light's block landed, so it is computed in panel-local space
    # while only the sample coordinate comes from the block rect.
    require(
        panel,
        "vec2 sampleUv = mix(pc.uvRect.xy, pc.uvRect.zw, fragLocal);",
        "the sampled coordinate spans the light's atlas block",
    )
    require_compact(
        panel,
        "float tileIndex = floor(fragLocal.x * divisions)"
        " + floor(fragLocal.y * divisions) * divisions;",
        "the cascade grid is panel-local, not atlas-local",
    )
    if "uvRect" in panel.split("float tileIndex")[1]:
        raise AssertionError(
            "The panel's cascade grid must not depend on the block rect, or it"
            " would drift with the light's atlas allocation"
        )

    pin = read("tools/tests/vk_shader_header_pin.py")
    committed = read("src/renderer/Vulkan/shaders/gui_shaders_spv.h")
    for shader, array in (
        ("shadow_debug_overlay.vert", "vk_shadow_debug_overlay_vert_spv"),
        ("shadow_debug_overlay.frag", "vk_shadow_debug_overlay_frag_spv"),
        ("shadow_debug_overlay_point.frag", "vk_shadow_debug_overlay_point_frag_spv"),
        ("shadow_debug_overlay_text.frag", "vk_shadow_debug_overlay_text_frag_spv"),
    ):
        require(pin, f'"{shader}"', "every overlay shader is pinned to the committed header")
        require(committed, f"{array}[]", "every overlay shader is embedded in the committed header")


def validate_projection_cache_precision_and_derivatives() -> None:
    # Numerical regressions execute in rendererShadowProjectedDiagnosticSelfTest.
    # Pin both integrations so neither can revert to its quantized cache helper.
    for path, helper, signature in (
        ("src/renderer/draw_arb2.cpp", "static int RB_ShadowMapHashFloat(",
         "static int RB_ShadowMapBuildPassSignatureForView("),
        ("src/renderer/Vulkan/vk_ShadowMap.cpp", "static int VK_ShadowMap_HashFloat(",
         "static int VK_ShadowMap_BuildPassSignatureForView("),
    ):
        source = read(path)
        require_compact(braced_body(source, helper, path),
                        "return R_ShadowMapHashFloat( hash, value );", path)
        require(braced_body(source, signature, path),
                "R_ShadowMapProjectedStateHash( hash, projectedState )", path)

    if "static int R_ShadowMapHashFloat(" in read("src/renderer/Interaction.cpp"):
        raise AssertionError("Caster transforms must use the shared exact float hash")

    require_compact(read("src/renderer/Vulkan/vk_Interactions.cpp"),
                    "const bool shadowingEnabled = r_shadows.GetBool() "
                    "&& vLight->lightShader->LightCastsShadows() "
                    "&& ( vLight->lightDef == NULL || !vLight->lightDef->parms.noShadows )",
                    "authored no-shadows lights never require a shadow fallback")

    library = read("src/renderer/ModernGLShaderLibrary.cpp")
    if "dFdx(depth)" in library or "dFdy(depth)" in library:
        raise AssertionError("Shadow depth derivatives must not execute inside light/cascade branches")
    require(library, "ModernClusterPrepareShadowDerivatives(viewPosition);",
            "deferred receiver derivatives")
    if library.count("ModernClusterPrepareShadowDerivatives(ModernClusterFromEyeSpace(vViewPosition));") != 2:
        raise AssertionError("Opaque/alpha-test and transparent forward receivers both need derivatives")
    forward = library[library.index("if ( kind == MODERN_GL_SHADER_CLUSTERED_FORWARD_OPAQUE ||"):]
    require_order(forward, ("ModernClusterPrepareShadowDerivatives(", "{ discard; }"),
                  "forward derivatives precede alpha discard")
    require(library, "shadowMatrix * vec4(gModernShadowPositionDx, 0.0)",
            "directional derivative excludes depth-plane translation")


def validate_gl_shadow_coverage_contract() -> None:
    source = read("src/renderer/draw_arb2.cpp")
    schedule = braced_body(source, "static shadowMapSchedule_t RB_ShadowMapSchedulePass(", "GL scheduling")
    require(schedule, "shadowMapIncompleteStencilMask", "map-only casters override optional budgets")
    require(schedule, "shadowMapPrelightStencilRequiredMask", "prelight coverage participates in scheduling")
    require(schedule, "subviewPolicy >= 2 && !mapRequired", "subview stencil fallback requires coverage")
    require(schedule, "( budgetExhausted || admissionDenied || subviewDenied ) && !mapRequired",
            "budget/admission fallback requires coverage")
    # Changed membership (a stopped door starts moving or a portal reveals
    # another caster) must never turn a signature miss into resident reuse.
    denied = braced_body(schedule,
        "if ( ( budgetExhausted || admissionDenied || subviewDenied ) && !mapRequired )",
        "GL denied update")
    if "SHADOWMAP_SCHEDULE_REUSE" in denied or "CacheEntry" in denied:
        raise AssertionError("Budget/subview misses must not revive obsolete door depth")
    require(denied, "schedule.action = SHADOWMAP_SCHEDULE_FALLBACK;", "current complete stencil coverage")
    admission = braced_body(source, "static void RB_ShadowMapBuildUpdateAdmissions(", "GL admission")
    require(admission, "candidates[i].cost > remaining && g_shadowMapAdmittedLightCount > 0",
            "a two-pass light can make progress within a budget of one")
    require(read("src/renderer/Vulkan/vk_ShadowMap.h"), "VK_SHADOW_MAX_LIGHTS = 256;",
            "stock door/portal views exceed the old 64-light table")
    require(read("src/renderer/Vulkan/vk_ShadowMap.cpp"),
            "VK_SHADOW_MAX_ADMITTED_LIGHTS = VK_SHADOW_MAX_LIGHTS;",
            "update admission must cover the expanded light table")
    for chain, render in (("RB_ShadowMapDrawCasterChain", "RB_RenderShadowMap"),
                          ("RB_PointShadowMapDrawCasterChain", "RB_RenderPointShadowMap")):
        body = braced_body(source, f"static bool {chain}(", chain)
        require_compact(body, "if ( !RB_ShadowMapResolveCasterDrawData( surf, casterGeo, ambientCache ) ) { return false; }",
                        "any missing caster fails its ownership")
        render_body = braced_body(source, f"static bool {render}(", render)
        for casters in ("primaryCasters", "secondaryCasters", "tertiaryCasters", "quaternaryCasters"):
            require(render_body, f"allCastersRendered &= {chain}( {casters},", "all four caster chains participate")
        require(render_body, "return allCastersRendered;", "partial maps are never published or sampled")


def validate_map_investigation_repairs() -> None:
    interaction = read("src/renderer/Interaction.cpp")
    material = read("src/renderer/Material.cpp")
    for directive in ('else if ( !token.Icmp( "noShadows" ) )',
                      'else if ( !token.Icmp( "DECAL_MACRO" ) )'):
        policy = braced_body(material, directive, "authored shadow opt-out")
        require(policy, "MF_NOSHADOWS | MF_NOSHADOWS_EXPLICIT", "authored shadow opt-out provenance")
    stencil_policy = braced_body(interaction, "static bool R_TranslucentStencilCasterEligible(",
                                "translucent stencil admission")
    require(stencil_policy, "!shader->ExplicitlyDisablesShadows()", "translucent options preserve noShadows")
    for signature in ("void idInteraction::CreateInteraction(",
                      "static bool R_ShadowMapShaderCanCastStencilParityTranslucent("):
        require(braced_body(interaction, signature, "translucent caster policy consumer"),
                "R_TranslucentStencilCasterEligible(", "shared stencil/map translucent admission")
    moments = braced_body(interaction, "static bool R_ShadowMapMaterialPolicyCanCastTranslucent(",
                         "translucent moment admission")
    require(moments, "!policy.explicitlyDisablesShadows", "moment casters preserve authored opt-outs")
    require_compact(interaction, """
        const bool shadowMapCasterPolicyActive =
            r_shadows.GetBool() && r_useShadowMap.GetBool() &&
            ( !vLight->pointLight || vLight->parallel || r_shadowMapPointLights.GetBool() );
        """, "shadows-off frames must not latch missing-caster fallback")
    executor = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    point_pipeline = braced_body(executor, "VkPipeline VK_Exec_PointCasterPipeline(", "point caster pipeline")
    if "depthClampSupported" in point_pipeline:
        raise AssertionError("Point casters must clip geometry crossing the light origin")
    for shader in ("shadow_caster.frag", "shadow_point_caster.frag"):
        source = read("src/renderer/Vulkan/shaders/" + shader)
        body = braced_body(source, "void main()", shader)
        alpha = body.index("if (pc.params.x != 0.0)")
        if body.index("dFdx(") > alpha or body.index("dFdy(") > alpha:
            raise AssertionError(f"{shader}: caster derivatives must precede cutout discard")
    gl = read("src/renderer/draw_arb2.cpp")
    if "GL_SelectTextureNoClient( 6 + i )" in gl:
        raise AssertionError("Moment unit 8 must not index the eight-unit legacy state array")
    binding = braced_body(gl, "static void RB_ShadowMapBindMomentTexture(", "moment binding")
    require(binding, "unit < MAX_MULTITEXTURE_UNITS", "tracked moment unit bounds")
    require(binding, "glBindTexture( target,", "extra GLSL moment sampler binding")
    require(binding, "glActiveTextureARB( GL_TEXTURE0_ARB );", "extra sampler restores tracked active unit")


def validate_live_cutout_and_point_culling_contract() -> None:
    gl = read("src/renderer/draw_arb2.cpp")
    cache_view = braced_body(gl, "bool RB_ShadowMapPrepareCacheView(", "shadow cache view ownership")
    require_compact(cache_view, "if ( viewDef == NULL || viewDef->renderWorld == NULL ) { return false; }",
                    "2D views must not evict the world shadow cache")
    require_order(cache_view, ("viewDef->renderWorld == NULL", "g_projectedShadowMapCache[i].valid = false"),
                  "ignore non-world views before cache mutation")
    interaction = read("src/renderer/Interaction.cpp")
    emitter = braced_body(interaction, "static bool R_CachedShouldSkipPointLightEmitterCaster(", "emitter exception")
    require_compact(emitter, "!( sint->shadowStencilEligible && !sint->shadowStencilUsesPrelight && sint->shadowTris != NULL )",
                    "real emitter blockers must remain in depth maps")
    for path, signature in (
        ("src/renderer/draw_arb2.cpp", "static int RB_ShadowMapBuildPassSignatureForView("),
        ("src/renderer/Vulkan/vk_ShadowMap.cpp", "static int VK_ShadowMap_BuildPassSignatureForView("),
    ):
        body = braced_body(read(path), signature, "static depth identity")
        for live_count in ("shadowMapDynamicCasterCount", "shadowMapAlphaCasterCount", "shadowMapCasterCount"):
            if live_count in body:
                raise AssertionError(f"{path}: live caster counts must not invalidate opaque static depth")
    vk = read("src/renderer/Vulkan/vk_ShadowMap.cpp")
    point = braced_body(vk, "static int VK_ShadowMap_DrawPointCasterChain(", "point face rejection")
    require_order(point, ("R_ShadowMapCasterOutsidePointFace", "VK_Exec_BindTriGeometry"),
                  "reject irrelevant faces before geometry binding/upload")
    for gate_name in ("VK_ShadowMap_StaticCacheable", "VK_ShadowMap_StaticCacheableReadOnly"):
        gate = braced_body(vk, "static bool " + gate_name + "(", gate_name)
        if "shadowMapAlphaCasterCount > 0" in gate:
            raise AssertionError("Live cutouts must allow projected static depth composition")
    prepare = braced_body(vk, "int VK_ShadowMap_PrepareViewLights(", "prepare shadow maps")
    if "VK_ShadowMap_ReportViewCache" in prepare:
        raise AssertionError("Reports before rendering hide actual cube-face and composition costs")
    diagnostics = braced_body(vk, "void VK_ShadowMap_DebugOverlayDraw(", "completed-view diagnostics")
    require_order(diagnostics, ("VK_ShadowMap_ReportViewCache", "!r_shadowMapDebugOverlay.GetBool()"),
                  "completed-view reports work with visual overlay disabled")


def validate_ci_registration() -> None:
    validator = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")

    if validator.count("renderer_vulkan_shadow_compatibility.py") != 1:
        raise AssertionError(
            "Local validation runner must register the Vulkan shadow compatibility test exactly once"
        )

    for workflow, context in (
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
    ):
        if workflow.count(TEST_PATH) != 2:
            raise AssertionError(f"{context} must compile and directly run {TEST_PATH}")
        require(workflow, f"python {TEST_PATH}", context)


def main() -> None:
    validate_shadow_effectiveness_image_gate()
    validate_shared_interaction_shadow_runtime_contract()
    validate_shared_interaction_shadow_profile_contract()
    validate_runtime_failure_gates()
    validate_receiver_ownership_split()
    validate_shadow_depth_format_selection()
    validate_csm_atlas_and_receiver_contract()
    validate_shadow_descriptor_abi()
    validate_shadow_filtering_contract()
    validate_point_receiver_world_bias_contract()
    validate_shadow_contact_and_gl_robustness_contract()
    validate_exact_static_cache_and_admission_contract()
    validate_dynamic_caster_composition_contract()
    validate_csm_static_cache_contract()
    validate_shadow_debug_mode_contract()
    validate_update_admission_contract()
    validate_shadow_report_and_storage_honesty()
    validate_shadow_gpu_timing_contract()
    validate_packed_shadow_geometry()
    validate_fail_closed_target_and_stencil_behavior()
    validate_shadow_debug_overlay_contract()
    validate_projection_cache_precision_and_derivatives()
    validate_gl_shadow_coverage_contract()
    validate_map_investigation_repairs()
    validate_live_cutout_and_point_culling_contract()
    validate_ci_registration()
    print("renderer_vulkan_shadow_compatibility: ok")


if __name__ == "__main__":
    main()
