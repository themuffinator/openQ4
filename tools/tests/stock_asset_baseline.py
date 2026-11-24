#!/usr/bin/env python3
"""Focused unit/contract tests for the P0 stock-asset baseline harness."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import os
import struct
import sys
import tempfile
from pathlib import Path
from types import ModuleType
from zipfile import ZipFile


ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = ROOT / "tools" / "validation" / "stock_asset_baseline.py"


def load_tool() -> ModuleType:
    spec = importlib.util.spec_from_file_location("openq4_stock_asset_baseline", TOOL_PATH)
    if spec is None or spec.loader is None:
        raise AssertionError("could not load stock_asset_baseline.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def cvar_value(arguments: list[str], name: str) -> str | None:
    for index in range(len(arguments) - 2):
        if arguments[index] == "+set" and arguments[index + 1] == name:
            return arguments[index + 2]
    return None


def timing_marker(map_name: str) -> str:
    return (
        f"OPENQ4_FRAME_TIMING_V1 map={map_name} backend=opengl profile=baseline "
        "cpuSamples=256 cpuP50Us=7000 cpuP95Us=11000 cpuP99Us=14000 "
        "gpuAvailable=1 gpuSamples=252 gpuP50Us=5000 gpuP95Us=8000 gpuP99Us=10000"
    )


def patterned_rgb(width: int, height: int) -> bytes:
    pixels = bytearray(width * height * 3)
    for y in range(height):
        for x in range(width):
            index = (y * width + x) * 3
            pixels[index] = (x * 17 + y * 43 + x * y * 3) & 0xFF
            pixels[index + 1] = (x * 71 + y * 11 + x * y * 5) & 0xFF
            pixels[index + 2] = (x * 29 + y * 97 + x * y * 7) & 0xFF
    return bytes(pixels)


def write_rgb_tga(path: Path, width: int, height: int, rgb: bytes) -> None:
    assert len(rgb) == width * height * 3
    header = bytearray(18)
    header[2] = 2
    struct.pack_into("<HH", header, 12, width, height)
    header[16] = 24
    header[17] = 0x20
    bgr = bytearray(len(rgb))
    for index in range(0, len(rgb), 3):
        bgr[index] = rgb[index + 2]
        bgr[index + 1] = rgb[index + 1]
        bgr[index + 2] = rgb[index]
    path.write_bytes(bytes(header) + bytes(bgr))


def write_minimal_tga(path: Path, width: int = 16, height: int = 8) -> None:
    write_rgb_tga(path, width, height, patterned_rgb(width, height))


def recursive_strip_rgb(width: int, height: int) -> bytes:
    assert width % 4 == 0 and height % 8 == 0
    pixels = bytearray(patterned_rgb(width, height))
    for factor in (2, 4):
        target_width = width // factor
        target_height = (height // 2) // factor
        target_y = height - height // factor
        block_pixels = factor * factor
        for y in range(target_height):
            for x in range(target_width):
                channels = [0, 0, 0]
                for block_y in range(factor):
                    for block_x in range(factor):
                        source_index = (
                            ((y * factor + block_y) * width + x * factor + block_x) * 3
                        )
                        for channel in range(3):
                            channels[channel] += pixels[source_index + channel]
                target_index = ((target_y + y) * width + x) * 3
                for channel in range(3):
                    pixels[target_index + channel] = (
                        channels[channel] + block_pixels // 2
                    ) // block_pixels
    return bytes(pixels)


def scene_rgb(width: int, height: int) -> bytes:
    """Low-frequency synthetic game frame with geometry, lighting and HUD detail."""

    pixels = bytearray(width * height * 3)
    for y in range(height):
        for x in range(width):
            index = (y * width + x) * 3
            red = 18 + 72 * x // max(1, width - 1) + 24 * y // max(1, height - 1)
            green = 28 + 45 * x // max(1, width - 1) + 36 * y // max(1, height - 1)
            blue = 42 + 28 * x // max(1, width - 1) + 54 * y // max(1, height - 1)
            if height * 5 // 9 < y:
                floor_band = ((x // max(1, width // 12)) + (y // max(1, height // 10))) & 1
                red = 48 + floor_band * 16 + 18 * x // max(1, width - 1)
                green = 42 + floor_band * 12
                blue = 38 + floor_band * 9
            if width * 7 // 16 < x < width * 9 // 16 and height // 5 < y < height * 4 // 5:
                red += 50
                green += 25
                blue -= 12
            distance = (x - width * 11 // 16) ** 2 + (y - height * 2 // 5) ** 2
            if distance < max(1, width // 14) ** 2:
                red += 95
                green += 70
                blue += 24
            if y >= height * 9 // 10 and width // 8 < x < width * 7 // 8:
                red = 18 + 80 * x // max(1, width - 1)
                green = 115 + 70 * x // max(1, width - 1)
                blue = 72
            pixels[index] = min(255, max(0, red))
            pixels[index + 1] = min(255, max(0, green))
            pixels[index + 2] = min(255, max(0, blue))
    return bytes(pixels)


def postprocess_variant(rgb: bytes, width: int, height: int) -> bytes:
    """Model mild exposure drift, a one-pixel scene shift and a changed effect."""

    output = bytearray(len(rgb))
    for y in range(height):
        for x in range(width):
            source_x = max(0, x - 1)
            source = (y * width + source_x) * 3
            target = (y * width + x) * 3
            output[target] = min(255, rgb[source] * 94 // 100 + 8)
            output[target + 1] = min(255, rgb[source + 1] * 97 // 100 + 5)
            output[target + 2] = min(255, rgb[source + 2] * 92 // 100 + 10)
            if width * 2 // 3 < x < width * 3 // 4 and height // 3 < y < height // 2:
                output[target] = min(255, output[target] + 20)
                output[target + 1] = min(255, output[target + 1] + 12)
    return bytes(output)


def compressed_exposure_variant(rgb: bytes) -> bytes:
    """Model a bright, low-contrast frame that needs shared exposure correction."""

    return bytes(
        min(255, max(0, (channel + 192) * 1000 // 2040)) for channel in rgb
    )


def severe_color_defect(rgb: bytes) -> bytes:
    """Apply a high-correlation red cast that exposure alone cannot repair."""

    output = bytearray(len(rgb))
    for index in range(0, len(rgb), 3):
        output[index] = min(255, rgb[index] + 90)
        output[index + 1] = rgb[index + 1] // 2
        output[index + 2] = rgb[index + 2] // 2
    return bytes(output)


def feedback_corruption(tool: ModuleType, rgb: bytes, width: int, height: int) -> bytes:
    """Overlay offset recursive views without matching the legacy strip signature."""

    output = bytearray(rgb)
    for target_x, target_y, target_width, target_height in (
        (5, height // 4, width * 3 // 4, height * 3 // 4),
        (2, height * 11 // 16, width * 3 // 8, height * 5 // 16),
    ):
        scaled = tool.center_aspect_resample_rgb(
            rgb, width, height, target_width, target_height
        )
        for y in range(target_height):
            if target_y + y >= height:
                break
            source = y * target_width * 3
            target = ((target_y + y) * width + target_x) * 3
            copy_width = min(target_width, width - target_x)
            output[target : target + copy_width * 3] = scaled[
                source : source + copy_width * 3
            ]
    return bytes(output)


def test_save_preview_coherence(tool: ModuleType, base: Path) -> None:
    screenshot = base / "coherent-shot.tga"
    preview = base / "coherent-preview.tga"
    screenshot_width, screenshot_height = 640, 360
    preview_width, preview_height = tool.SP_SAVE_PREVIEW_DIMENSIONS
    screenshot_rgb = scene_rgb(screenshot_width, screenshot_height)
    preview_rgb = tool.center_aspect_resample_rgb(
        screenshot_rgb,
        screenshot_width,
        screenshot_height,
        preview_width,
        preview_height,
    )
    write_rgb_tga(screenshot, screenshot_width, screenshot_height, screenshot_rgb)
    write_rgb_tga(preview, preview_width, preview_height, preview_rgb)
    comparison, failure = tool.save_preview_comparison(screenshot, preview)
    assert failure is None and comparison is not None
    assert comparison["referenceArtifact"] == "saveReferenceScreenshot"
    assert comparison["algorithm"].startswith("same-state-")
    assert comparison["lumaCorrelation"] > 0.99

    write_rgb_tga(
        preview,
        preview_width,
        preview_height,
        postprocess_variant(preview_rgb, preview_width, preview_height),
    )
    variant_comparison, failure = tool.save_preview_comparison(screenshot, preview)
    assert failure is None and variant_comparison is not None
    assert variant_comparison["lumaCorrelation"] > tool.SP_SAVE_PREVIEW_MIN_LUMA_CORRELATION

    write_rgb_tga(
        preview,
        preview_width,
        preview_height,
        compressed_exposure_variant(preview_rgb),
    )
    exposure_comparison, failure = tool.save_preview_comparison(screenshot, preview)
    assert failure is None and exposure_comparison is not None
    assert (
        exposure_comparison["meanAbsoluteRgbError"]
        > tool.SP_SAVE_PREVIEW_MAX_COMPENSATED_RGB_ERROR
    )
    assert (
        exposure_comparison["exposureCompensatedMeanAbsoluteRgbError"]
        <= tool.SP_SAVE_PREVIEW_MAX_COMPENSATED_RGB_ERROR
    )

    write_rgb_tga(
        preview,
        preview_width,
        preview_height,
        severe_color_defect(preview_rgb),
    )
    color_comparison, failure = tool.save_preview_comparison(screenshot, preview)
    assert color_comparison is not None
    assert color_comparison["lumaCorrelation"] > tool.SP_SAVE_PREVIEW_MIN_LUMA_CORRELATION
    assert failure is not None and "exposure-compensated" in failure

    corrupted_rgb = feedback_corruption(
        tool, preview_rgb, preview_width, preview_height
    )
    write_rgb_tga(preview, preview_width, preview_height, corrupted_rgb)
    assert tool.validate_tga(preview, tool.SP_SAVE_PREVIEW_DIMENSIONS) is None
    corrupted_comparison, failure = tool.save_preview_comparison(screenshot, preview)
    assert corrupted_comparison is not None
    assert failure is not None and "luma correlation" in failure

    final6 = (
        ROOT
        / ".tmp"
        / "stock-baseline"
        / "p0-20260819-save-preview-final6-sp-default"
        / "savepaths"
        / "sp"
        / "baseoq4"
    )
    final6_screenshot = final6 / "screenshots" / "stock-baseline" / "sp_after_load.tga"
    final6_preview = final6 / "savegames" / "StockBaselineSP.tga"
    final6_comparison = None
    if final6_screenshot.is_file() and final6_preview.is_file():
        final6_comparison, failure = tool.save_preview_comparison(
            final6_screenshot, final6_preview
        )
        assert final6_comparison is not None and failure is None
        assert final6_comparison["lumaCorrelation"] >= tool.SP_SAVE_PREVIEW_MIN_LUMA_CORRELATION
        assert (
            final6_comparison["exposureCompensatedMeanAbsoluteRgbError"]
            <= tool.SP_SAVE_PREVIEW_MAX_COMPENSATED_RGB_ERROR
        )

        legacy_preview = (
            ROOT
            / ".tmp"
            / "stock-baseline"
            / "p0-20260819-save-preview-final6-sp-legacy50"
            / "savepaths"
            / "sp"
            / "baseoq4"
            / "savegames"
            / "StockBaselineSP.tga"
        )
        if legacy_preview.is_file():
            default_decoded = tool.decode_tga_rgb(final6_preview.read_bytes())
            legacy_decoded = tool.decode_tga_rgb(legacy_preview.read_bytes())
            assert not isinstance(default_decoded, str)
            assert not isinstance(legacy_decoded, str)
            default_width, default_height, default_rgb = default_decoded
            legacy_width, legacy_height, legacy_rgb = legacy_decoded
            analysis_width, analysis_height = tool.SP_SAVE_PREVIEW_ANALYSIS_DIMENSIONS
            default_normalized = tool.center_aspect_resample_rgb(
                default_rgb,
                default_width,
                default_height,
                analysis_width,
                analysis_height,
            )
            legacy_normalized = tool.center_aspect_resample_rgb(
                legacy_rgb,
                legacy_width,
                legacy_height,
                analysis_width,
                analysis_height,
            )
            preview_metrics = tool.rgb_similarity_metrics(
                default_normalized, legacy_normalized
            )
            assert not isinstance(preview_metrics, str)
            assert preview_metrics[0] >= tool.SP_SAVE_PREVIEW_MIN_LUMA_CORRELATION
            compensated_metrics = tool.exposure_compensated_rgb_metrics(
                default_normalized, legacy_normalized
            )
            assert not isinstance(compensated_metrics, str)
            assert compensated_metrics[2] <= tool.SP_SAVE_PREVIEW_MAX_COMPENSATED_RGB_ERROR

    final12 = (
        ROOT
        / ".tmp"
        / "stock-baseline"
        / "p0-20260819-final12"
        / "savepaths"
        / "sp"
        / "baseoq4"
    )
    final12_screenshot = final12 / "screenshots" / "stock-baseline" / "sp_after_load.tga"
    final12_preview = final12 / "savegames" / "StockBaselineSP.tga"
    if final12_screenshot.is_file() and final12_preview.is_file():
        assert tool.validate_tga(
            final12_preview, tool.SP_SAVE_PREVIEW_DIMENSIONS
        ) is None
        final12_comparison, failure = tool.save_preview_comparison(
            final12_screenshot, final12_preview
        )
        assert final12_comparison is not None and failure is None
        assert final12_comparison["lumaCorrelation"] >= tool.SP_SAVE_PREVIEW_MIN_LUMA_CORRELATION
        assert (
            final12_comparison["meanAbsoluteRgbError"]
            > tool.SP_SAVE_PREVIEW_MAX_COMPENSATED_RGB_ERROR
        )
        assert (
            final12_comparison["exposureCompensatedMeanAbsoluteRgbError"]
            <= tool.SP_SAVE_PREVIEW_MAX_COMPENSATED_RGB_ERROR
        )

    final11 = (
        ROOT
        / ".tmp"
        / "stock-baseline"
        / "p0-20260819-final11"
        / "savepaths"
        / "sp"
        / "baseoq4"
    )
    final11_screenshot = final11 / "screenshots" / "stock-baseline" / "sp_after_load.tga"
    final11_preview = final11 / "savegames" / "StockBaselineSP.tga"
    if final11_screenshot.is_file() and final11_preview.is_file():
        final11_comparison, failure = tool.save_preview_comparison(
            final11_screenshot, final11_preview
        )
        assert final11_comparison is not None
        assert final11_comparison["lumaCorrelation"] < tool.SP_SAVE_PREVIEW_MIN_LUMA_CORRELATION
        assert failure is not None and "luma correlation" in failure
        if final6_comparison is not None:
            assert (
                final6_comparison["lumaCorrelation"]
                - final11_comparison["lumaCorrelation"]
                >= 0.10
            )


def test_asset_inventory_and_comparison(tool: ModuleType, base: Path) -> None:
    asset_root = base / "assets"
    q4base = asset_root / "q4base"
    q4mp = asset_root / "q4mp"
    q4base.mkdir(parents=True)
    q4mp.mkdir()
    (q4base / "pak001.pk4").write_bytes(b"stock-one")
    (q4base / "zpak_english.pk4").write_bytes(b"language")
    (q4mp / "game300.pk4").write_bytes(b"mp")

    records = tool.collect_pk4s(asset_root)
    assert [item["path"] for item in records] == [
        "q4base/pak001.pk4",
        "q4base/zpak_english.pk4",
        "q4mp/game300.pk4",
    ]
    assert tool.compare_asset_records(records, records) == []
    changed = json.loads(json.dumps(records))
    changed[0]["sha256"] = "0" * 64
    failures = tool.compare_asset_records(changed, records)
    assert len(failures) == 1 and "SHA" not in failures[0] and "sha256 differs" in failures[0]
    (q4base / "generated.cfg").write_text("set test 1", encoding="utf-8")
    loose = tool.collect_loose_asset_files(asset_root)
    assert [item["path"] for item in loose] == ["q4base/generated.cfg"]

    contaminated = base / "contaminated"
    (contaminated / "q4base").mkdir(parents=True)
    (contaminated / "q4base" / "pak001.pk4").write_bytes(b"stock")
    (contaminated / "baseoq4").mkdir()
    (contaminated / "baseoq4" / "override.cfg").write_text("override", encoding="utf-8")
    try:
        tool.collect_pk4s(contaminated)
    except ValueError as exc:
        assert "non-empty" in str(exc) and "unapproved content" in str(exc)
    else:
        raise AssertionError("non-empty asset-root/baseoq4 must fail closed")

    original_link_check = tool.is_link_or_junction
    tool.is_link_or_junction = lambda path: path.name == "q4base"
    try:
        try:
            tool.validate_asset_root(asset_root)
        except ValueError as exc:
            assert "--allow-asset-dir-links" in str(exc)
        else:
            raise AssertionError("linked asset directories require explicit opt-in")
        tool.validate_asset_root(asset_root, allow_asset_dir_links=True)
        views = tool.asset_directory_views(asset_root)
        assert next(view for view in views if view["path"] == "q4base")["linked"] is True
    finally:
        tool.is_link_or_junction = original_link_check

    stale = base / "stale-evidence"
    stale.mkdir()
    (stale / "old.log").write_text("stale", encoding="utf-8")
    try:
        tool.prepare_output_directory(stale)
    except ValueError as exc:
        assert "new or empty" in str(exc)
    else:
        raise AssertionError("non-empty evidence directories must be rejected")

    collision_assets = base / "collision-assets"
    collision_runtime = base / "collision-runtime"
    (collision_assets / "q4base").mkdir(parents=True)
    (collision_runtime / "baseoq4").mkdir(parents=True)
    with ZipFile(collision_assets / "q4base" / "pak001.pk4", "w") as archive:
        archive.writestr("guis/mainmenu.gui", "retail")
        archive.writestr("materials/retail_only.mtr", "retail")
    with ZipFile(collision_runtime / "baseoq4" / "pak0.pk4", "w") as archive:
        archive.writestr("GUIS/MainMenu.gui", "overlay")
        archive.writestr("materials/openq4_only.mtr", "overlay")
    collision_records = tool.collect_retail_path_collisions(
        collision_assets,
        tool.collect_pk4s(collision_assets),
        collision_runtime,
        tool.collect_overlay_pk4s(collision_runtime),
    )
    assert collision_records == [
        {
            "path": "guis/mainmenu.gui",
            "retailPk4s": ["q4base/pak001.pk4"],
            "openQ4OverlayPk4s": ["baseoq4/pak0.pk4"],
        }
    ]
    report_dir = base / "collision-report"
    report_dir.mkdir()
    tool.write_reports(
        report_dir,
        {
            "schemaVersion": tool.SCHEMA_VERSION,
            "status": "pass",
            "generatedUtc": "2026-08-19T00:00:00Z",
            "git": tool.git_state(ROOT),
            "mpPort": 28140,
            "runtimeRoot": str(collision_runtime.resolve()),
            "runtimeFiles": [],
            "expectedAssets": {"supplied": True, "sha256": "1" * 64},
            "assets": {
                "compatibilityModel": tool.ASSET_COMPATIBILITY_MODEL,
                "root": str(collision_assets.resolve()),
                "allowLinkedGameDirectories": False,
                "directoryViews": tool.asset_directory_views(collision_assets),
                "stockPk4s": tool.collect_pk4s(collision_assets),
                "looseFiles": [],
                "openQ4OverlayPk4s": tool.collect_overlay_pk4s(collision_runtime),
                "openQ4OverlayLooseFiles": [],
                "retailArchiveBytesMatchExpected": True,
                "retailPathNamespaceUntouched": False,
                "retailPathCollisionCount": 1,
                "retailPathCollisions": collision_records,
            },
            "assetComparisonFailures": [],
            "preflightFailures": [],
            "postCaptureVerificationFailures": [],
            "results": [],
        },
    )
    report_markdown = (report_dir / "stock_asset_baseline_report.md").read_text(
        encoding="utf-8"
    )
    assert "not an overlay-free or stock-only run" in report_markdown
    assert "`baseoq4/pak0.pk4` supersedes 1 retail virtual path" in report_markdown
    assert "clean logs" not in report_markdown.casefold()
    assert "none of the harness denylist classes" in report_markdown
    assert "engine `ERROR` records" in report_markdown
    assert "shader compile/program-link failures" in report_markdown
    assert "Vulkan validation messages or VUIDs" in report_markdown
    assert "OpenGL errors" in report_markdown
    assert "does not assert warning-free logs" in report_markdown


def test_plan_is_windowed_and_engine_only(tool: ModuleType, base: Path) -> None:
    root = base / "repo"
    output = base / "evidence"
    asset_root = base / "assets"
    (root / ".install").mkdir(parents=True)
    plans = tool.prepare_plans(root, asset_root, output, 1280, 720, 28140)
    assert set(plans) == {"sp-capture", "sp-demo-playback", "mp-server", "mp-client"}
    for plan in plans.values():
        assert cvar_value(plan.args, "r_fullscreen") == "0"
        assert cvar_value(plan.args, "r_borderless") == "0"
        assert cvar_value(plan.args, "r_borderlessDefaultMigrated") == "1"
        assert cvar_value(plan.args, "r_fullscreenDesktop") == "0"
        assert cvar_value(plan.args, "r_windowWidth") == "1280"
        assert cvar_value(plan.args, "r_windowHeight") == "720"
        assert cvar_value(plan.args, "r_mode") == "-1"
        assert cvar_value(plan.args, "r_customWidth") == "1280"
        assert cvar_value(plan.args, "r_customHeight") == "720"
        assert cvar_value(plan.args, "r_renderApi") == "gl"
        assert cvar_value(plan.args, "r_rendererSharedGui") == "0"
        assert cvar_value(plan.args, "r_rendererSharedInWorldGui") == "0"
        assert cvar_value(plan.args, "r_rendererSharedCinematicPost") == "0"
        assert cvar_value(plan.args, "r_rendererSharedSpecialFrame") == "0"
        assert cvar_value(plan.args, "r_rendererSharedWorldAmbient") == "0"
        assert cvar_value(plan.args, "r_rendererSharedWorldInteraction") == "0"
        assert cvar_value(plan.args, "r_rendererSharedWorldFogBlend") == "0"
        assert cvar_value(plan.args, "r_rendererSharedSubview") == "0"
        assert cvar_value(plan.args, "r_rendererSharedDeform") == "0"
        assert cvar_value(plan.args, "r_rendererBenchmarkPreset") == "baseline"
        assert cvar_value(plan.args, "r_rendererMetrics") == "0"
        assert cvar_value(plan.args, "r_rendererGpuTimers") == "1"
        assert cvar_value(plan.args, "fs_devpath") == str(plan.savepath)
        assert cvar_value(plan.args, "fs_cdpath") is None
        assert plan.args.count("+vid_restart") == 1
        assert plan.args.index("+vid_restart") > plan.args.index("r_windowHeight")
        record = tool.plan_record(plan)
        assert record["windowed"] is True
        assert record["captureMethod"] == "engine screenshot command"
        if plan.role_id.startswith("mp-"):
            assert cvar_value(plan.args, "ui_autoJoin") == "1"
            assert plan.args.index("ui_autoJoin") < plan.args.index("+vid_restart")
        if plan.role_id == "mp-server":
            assert cvar_value(plan.args, "si_pure") == "1"
            assert cvar_value(plan.args, "net_serverAllowServerMod") == "0"

    sp_stage1 = (output / "savepaths" / "sp" / "baseoq4" / "stock-baseline" / "sp_stage1.cfg").read_text(encoding="utf-8")
    sp_stage2 = (output / "savepaths" / "sp" / "baseoq4" / "stock-baseline" / "sp_after_load.cfg").read_text(encoding="utf-8")
    for token in ("recordDemo stock_baseline_sp", "saveGame StockBaselineSP", "loadGame StockBaselineSP"):
        assert token in sp_stage1
    sp_stage1_lines = sp_stage1.splitlines()
    save_reference_line = sp_stage1_lines.index(
        'screenshot "screenshots/stock-baseline/sp_before_save.tga"'
    )
    save_game_line = sp_stage1_lines.index("saveGame StockBaselineSP")
    assert save_reference_line + 1 == save_game_line
    assert plans["sp-capture"].expected.count(
        (
            "saveReferenceScreenshot",
            "baseoq4/screenshots/stock-baseline/sp_before_save.tga",
        )
    ) == 1
    assert "OPENQ4_STOCK_BASELINE_SP_SAVE_LOAD_COMPLETE" in sp_stage2
    assert 'screenshot "screenshots/stock-baseline/sp_after_load.tga"' in sp_stage2
    mp_server_cfg = (output / "savepaths" / "mp-server" / "baseoq4" / "stock-baseline" / "server.cfg").read_text(encoding="utf-8")
    mp_client_cfg = (output / "savepaths" / "mp-client" / "baseoq4" / "stock-baseline" / "client.cfg").read_text(encoding="utf-8")
    assert "waitMsec 120000" in mp_server_cfg
    assert "waitMsec 10000" in mp_client_cfg
    assert "rendererBenchmarkCapture" in mp_server_cfg
    assert "rendererBenchmarkCapture" in mp_client_cfg
    assert "r_rendererMetrics 1" in mp_server_cfg
    assert "r_rendererMetrics 0" in mp_server_cfg

    def assert_canonical_display_before_sampling(payload: str) -> None:
        lines = payload.splitlines()
        reset_index = lines.index("framePacingReset")
        for name, value in tool.display_contract()["cvars"].items():
            matches = [line for line in lines[:reset_index] if line.startswith(f"{name} ")]
            assert matches == [f"{name} {value}"]

    assert_canonical_display_before_sampling(sp_stage2)
    assert_canonical_display_before_sampling(mp_server_cfg)
    assert_canonical_display_before_sampling(mp_client_cfg)
    assert "openq4_joinGame" not in mp_client_cfg
    assert "openq4_assertMPClientActive" in mp_client_cfg
    assert "openq4_assertMPGameplayView" in mp_client_cfg
    mp_client_lines = mp_client_cfg.splitlines()
    screenshot_line = mp_client_lines.index('screenshot "screenshots/stock-baseline/mp_client.tga"')
    assertion_lines = [
        index for index, line in enumerate(mp_client_lines) if line == "openq4_assertMPClientActive"
    ]
    assert len(assertion_lines) == 3
    assert assertion_lines[-2] < screenshot_line < assertion_lines[-1]
    view_assertion_lines = [
        index for index, line in enumerate(mp_client_lines) if line == "openq4_assertMPGameplayView"
    ]
    assert len(view_assertion_lines) == 3
    assert view_assertion_lines[-2] < screenshot_line < view_assertion_lines[-1]
    assert mp_client_lines[view_assertion_lines[-1] + 1] == (
        "echo OPENQ4_STOCK_BASELINE_MP_CLIENT_COMPLETE"
    )
    assert "expected exact budget workload" in tool.runtime_window_failure(
        "  [0] * Test Display (contentScale 1.50)\nMODE: -1, 1920 x 1080 windowed hz:N/A\n",
        1280,
        720,
    )
    assert tool.runtime_window_failure(
        "MODE: -1, 1280 x 720 windowed hz:N/A\n", 1280, 720
    ) is None
    assert "selector" in tool.runtime_window_failure(
        "MODE: 5, 1280 x 720 windowed hz:N/A\n", 1280, 720
    )
    assert "borderless" in tool.runtime_window_failure(
        "MODE: 5, 2560 x 1440 borderless hz:N/A\n", 1280, 720
    )
    assert "missing" in tool.runtime_window_failure("no renderer evidence\n")
    assert "screenshot-write" in tool.mp_client_active_proof_failure(
        tool.MP_CLIENT_ACTIVE_MARKER + "\nOPENQ4_STOCK_BASELINE_MP_CLIENT_COMPLETE\n"
    )
    legacy_active_line = (
        tool.MP_CLIENT_ACTIVE_MARKER
        + " client=1 spectating=0 wantSpectate=0 ingame=1\n"
    )
    view_line = tool.MP_CLIENT_VIEW_MARKER + " gui=0\n"
    screenshot_write = "Wrote screenshots/stock-baseline/mp_client.tga\n"
    assert "two exact" in tool.mp_client_active_proof_failure(
        legacy_active_line
        + view_line
        + screenshot_write
        + legacy_active_line
        + view_line
        + "OPENQ4_STOCK_BASELINE_MP_CLIENT_COMPLETE\n"
    )
    active_line = (
        tool.MP_CLIENT_ACTIVE_MARKER
        + " client=1 spectating=0 wantSpectate=0 ingame=1 menu=0 disableHud=0\n"
    )
    completion_line = "OPENQ4_STOCK_BASELINE_MP_CLIENT_COMPLETE\n"
    assert tool.mp_client_active_proof_failure(
        active_line + view_line + screenshot_write + active_line + view_line + completion_line
    ) is None
    assert "bracket" in tool.mp_client_active_proof_failure(
        active_line + view_line + active_line + view_line + screenshot_write + completion_line
    )
    assert "bracket" in tool.mp_client_active_proof_failure(
        screenshot_write + active_line + view_line + active_line + view_line + completion_line
    )
    assert "2560x1440" in tool.runtime_window_failure(
        "  [0] * Test Display (contentScale 1.50)\nMODE: -1, 2560 x 1440 windowed hz:N/A\n",
        1280,
        720,
    )

    with contextlib.redirect_stderr(io.StringIO()):
        try:
            tool.parse_args(["--asset-root", str(asset_root), "--width", "640", "--height", "480"])
        except SystemExit as exc:
            assert exc.code == 2
        else:
            raise AssertionError("noncanonical real baseline dimensions must fail")
    dry_custom = tool.parse_args(
        [
            "--asset-root",
            str(asset_root),
            "--dry-run",
            "--width",
            "640",
            "--height",
            "480",
        ]
    )
    assert dry_custom.dry_run and (dry_custom.width, dry_custom.height) == (640, 480)

    source = TOOL_PATH.read_text(encoding="utf-8")
    for forbidden in ("pyautogui", "ImageGrab", "BitBlt", "PrintWindow", "mss.mss", "pynput"):
        assert forbidden not in source


def test_diagnostic_authority_and_shared_mp_deadline(
    tool: ModuleType, base: Path
) -> None:
    log = base / "authoritative.log"
    stdout = base / "mirrored.stdout.txt"
    stderr = base / "mirrored.stderr.txt"
    active_line = (
        tool.MP_CLIENT_ACTIVE_MARKER
        + " client=1 spectating=0 wantSpectate=0 ingame=1 menu=0 disableHud=0\n"
    )
    view_line = tool.MP_CLIENT_VIEW_MARKER + " gui=0\n"
    screenshot_write = "Wrote screenshots/stock-baseline/mp_client.tga\n"
    completion_line = "OPENQ4_STOCK_BASELINE_MP_CLIENT_COMPLETE\n"
    valid_sequence = (
        active_line
        + view_line
        + screenshot_write
        + active_line
        + view_line
        + completion_line
    )
    log.write_text(valid_sequence, encoding="utf-8")
    stdout.write_text(valid_sequence, encoding="utf-8")
    stderr.write_text("", encoding="utf-8")
    authoritative, all_diagnostics = tool.collect_role_diagnostics(
        log, stdout, stderr
    )
    assert authoritative == valid_sequence
    assert all_diagnostics.count(screenshot_write.strip()) == 2
    assert tool.mp_client_active_proof_failure(authoritative) is None
    assert "exactly one" in tool.mp_client_active_proof_failure(all_diagnostics)

    # A valid mirrored stdout sequence must not repair broken ordering in the
    # authoritative engine log.
    log.write_text(
        active_line + view_line + active_line + view_line + screenshot_write + completion_line,
        encoding="utf-8",
    )
    authoritative, _ = tool.collect_role_diagnostics(log, stdout, stderr)
    assert "bracket" in tool.mp_client_active_proof_failure(authoritative)

    clock = [100.0]
    kill_times: list[float] = []

    class HangingProcess:
        def poll(self) -> None:
            return None

        def kill(self) -> None:
            kill_times.append(clock[0])

        def wait(self, timeout: int) -> int:
            assert timeout == 10
            return -9

    original_monotonic = tool.time.monotonic
    original_sleep = tool.time.sleep
    tool.time.monotonic = lambda: clock[0]
    tool.time.sleep = lambda seconds: clock.__setitem__(0, clock[0] + seconds)
    try:
        results = tool.wait_processes_until(
            {"server": HangingProcess(), "client": HangingProcess()}, 101.0
        )
    finally:
        tool.time.monotonic = original_monotonic
        tool.time.sleep = original_sleep
    assert results == {"server": (-9, True), "client": (-9, True)}
    assert len(kill_times) == 2
    assert all(abs(kill_time - 101.0) < 1e-9 for kill_time in kill_times)


def make_runtime_package(tool: ModuleType, runtime_dir: Path) -> tuple[Path, list[dict[str, object]]]:
    game_dir = runtime_dir / "baseoq4"
    game_dir.mkdir(parents=True)
    suffix = ".exe" if os.name == "nt" else ""
    shared = ".dll" if os.name == "nt" else (".dylib" if sys.platform == "darwin" else ".so")
    executable = runtime_dir / f"openQ4-client_{tool.host_arch()}{suffix}"
    executable.write_bytes(b"client")
    (runtime_dir / f"openQ4-ded_{tool.host_arch()}{suffix}").write_bytes(b"dedicated")
    (runtime_dir / f"openQ4-client_{tool.host_arch()}.pdb").write_bytes(b"symbols")
    (runtime_dir / f"renderer-gl_{tool.host_arch()}{shared}").write_bytes(b"renderer")
    (runtime_dir / f"OpenAL32{shared}").write_bytes(b"audio")
    (game_dir / f"game_{tool.host_arch()}{shared}").write_bytes(b"game")
    (game_dir / "mod.json").write_text("{}", encoding="utf-8")
    (game_dir / f"game_{tool.host_arch()}.pdb").write_bytes(b"symbols")
    records = tool.collect_runtime_files(runtime_dir, executable)
    assert {item["kind"] for item in records} >= {
        "clientExecutable",
        "gameModule",
        "dedicatedServerExecutable",
        "diagnosticSymbols",
        "runtimeLibrary",
    }
    loose, unexpected = tool.collect_overlay_loose_files(runtime_dir, records)
    assert unexpected == [] and any(item["path"] == "baseoq4/mod.json" for item in loose)
    unexpected_content = game_dir / "maps" / "override.map"
    unexpected_content.parent.mkdir()
    unexpected_content.write_text("override", encoding="utf-8")
    _, unexpected = tool.collect_overlay_loose_files(runtime_dir, records)
    assert any("override.map" in failure for failure in unexpected)
    unexpected_content.unlink()
    return executable, records


def test_tga_and_report_verification(tool: ModuleType, base: Path) -> None:
    screenshot = base / "shot.tga"
    write_minimal_tga(screenshot)
    assert tool.validate_tga(screenshot) is None
    assert tool.validate_tga(screenshot, (16, 8)) is None
    assert "differ" in tool.validate_tga(screenshot, (32, 16))
    screenshot.write_bytes(b"short")
    assert tool.validate_tga(screenshot) == "TGA header is truncated"
    blank = base / "blank.tga"
    write_rgb_tga(blank, 16, 8, bytes(16 * 8 * 3))
    assert "blank or near-solid" in tool.validate_tga(blank)
    recursive = base / "recursive.tga"
    write_rgb_tga(recursive, 64, 32, recursive_strip_rgb(64, 32))
    assert "recursive scaled-strip" in tool.validate_tga(recursive)
    malformed = base / "malformed.tga"
    write_minimal_tga(malformed)
    malformed.write_bytes(malformed.read_bytes() + b"trailing")
    assert "payload length" in tool.validate_tga(malformed)

    asset_root = base / "verify-assets"
    (asset_root / "q4base").mkdir(parents=True)
    stock = asset_root / "q4base" / "pak001.pk4"
    stock.write_bytes(b"retail")
    asset_records = tool.collect_pk4s(asset_root)
    runtime_dir = base / "runtime"
    runtime_executable, runtime_records = make_runtime_package(tool, runtime_dir)
    overlay_loose, unexpected = tool.collect_overlay_loose_files(runtime_dir, runtime_records)
    assert unexpected == []
    report_width, report_height = tool.DEFAULT_WIDTH, tool.DEFAULT_HEIGHT
    expected_assets_path = base / "expected-assets.json"
    expected_assets_path.write_text(
        json.dumps(
            {
                "schemaVersion": 1,
                "allowLinkedGameDirectories": False,
                "directoryViews": tool.asset_directory_views(asset_root),
                "stockPk4s": asset_records,
                "looseFiles": [],
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    budget_contract, budget_binding = tool.load_contract(tool.DEFAULT_CONTRACT_PATH)
    report: dict[str, object] = {
        "schemaVersion": tool.SCHEMA_VERSION,
        "status": "pass",
        "dryRun": False,
        "git": tool.git_state(ROOT),
        "budgetContract": budget_binding,
        "mpPort": 28140,
        "runtimeRoot": str(runtime_dir.resolve()),
        "expectedAssets": {
            "supplied": True,
            "path": str(expected_assets_path.resolve()),
            "sha256": tool.sha256_file(expected_assets_path),
        },
        "runtimeFiles": runtime_records,
        "safety": {
            "windowedOnly": True,
            "borderless": False,
            "windowSize": {"width": report_width, "height": report_height},
            "displayContract": tool.display_contract(),
            "engineScreenshotOnly": True,
            "operatingSystemCapture": False,
            "inputInjection": False,
        },
        "assets": {
            "compatibilityModel": tool.ASSET_COMPATIBILITY_MODEL,
            "root": str(asset_root.resolve()),
            "allowLinkedGameDirectories": False,
            "directoryViews": tool.asset_directory_views(asset_root),
            "stockPk4s": asset_records,
            "looseFiles": [],
            "openQ4OverlayPk4s": [],
            "openQ4OverlayLooseFiles": overlay_loose,
            "retailArchiveBytesMatchExpected": True,
            "retailPathNamespaceUntouched": True,
            "retailPathCollisionCount": 0,
            "retailPathCollisions": [],
        },
        "assetComparisonFailures": [],
        "preflightFailures": [],
        "postCaptureVerificationFailures": [],
        "plan": [],
        "results": [],
    }
    plans: list[dict[str, object]] = report["plan"]  # type: ignore[assignment]
    results: list[dict[str, object]] = report["results"]  # type: ignore[assignment]
    generated_plans = tool.prepare_plans(
        runtime_dir, asset_root, base, report_width, report_height, 28140
    )
    for role, contract in tool.ROLE_EVIDENCE_CONTRACT.items():
        savepath = base / "savepaths" / contract["saveDir"]
        plans.append(tool.plan_record(generated_plans[role]))
        artifacts: list[dict[str, object]] = []
        log = base / "savepaths" / contract["saveDir"] / "baseoq4" / "logs" / contract["logName"]
        log.parent.mkdir(parents=True, exist_ok=True)
        active_proofs = ""
        if role == "mp-client":
            active_line = (
                tool.MP_CLIENT_ACTIVE_MARKER
                + " client=1 spectating=0 wantSpectate=0 ingame=1 menu=0 disableHud=0\n"
            )
            view_line = tool.MP_CLIENT_VIEW_MARKER + " gui=0\n"
            active_proofs = (
                active_line
                + view_line
                + "Wrote screenshots/stock-baseline/mp_client.tga\n"
                + active_line
                + view_line
            )
        log.write_text(
            "  [0] * Test Display (contentScale 1.00)\n"
            f"MODE: -1, {report_width} x {report_height} windowed hz:N/A\n"
            + active_proofs
            + (
                timing_marker(contract["budget"]["map"]) + "\n"
                if contract.get("budget")
                else ""
            )
            + contract["marker"]
            + "\n",
            encoding="utf-8",
        )
        artifacts.append({"kind": "engineLog", **tool.file_record(log, base)})
        for kind, stream in (("processStdout", "stdout"), ("processStderr", "stderr")):
            path = base / f"{role}.{stream}.txt"
            path.write_text("", encoding="utf-8")
            artifacts.append({"kind": kind, **tool.file_record(path, base)})
        for kind, relative in contract["expected"].items():
            path = base / "savepaths" / contract["saveDir"] / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            if kind == "screenshot":
                if role == "sp-capture":
                    write_rgb_tga(
                        path,
                        report_width,
                        report_height,
                        patterned_rgb(report_width, report_height),
                    )
                else:
                    write_minimal_tga(path, report_width, report_height)
            elif kind == "saveReferenceScreenshot":
                write_rgb_tga(
                    path,
                    report_width,
                    report_height,
                    scene_rgb(report_width, report_height),
                )
            elif kind == "savePreview":
                reference_path = (
                    base
                    / "savepaths"
                    / contract["saveDir"]
                    / contract["expected"]["saveReferenceScreenshot"]
                )
                reference_decoded = tool.decode_tga_rgb(reference_path.read_bytes())
                assert not isinstance(reference_decoded, str)
                reference_width, reference_height, reference_rgb = reference_decoded
                preview_width, preview_height = tool.SP_SAVE_PREVIEW_DIMENSIONS
                write_rgb_tga(
                    path,
                    preview_width,
                    preview_height,
                    tool.center_aspect_resample_rgb(
                        reference_rgb,
                        reference_width,
                        reference_height,
                        preview_width,
                        preview_height,
                    ),
                )
            else:
                path.write_bytes(b"evidence")
            artifacts.append({"kind": kind, **tool.file_record(path, base)})
        result: dict[str, object] = {
            "role": role,
            "mode": generated_plans[role].mode,
            "status": "pass",
            "exitCode": 0,
            "timedOut": False,
            "failures": [],
            "artifacts": artifacts,
            "budgetEvidence": {},
        }
        if contract.get("budget"):
            identity = contract["budget"]
            evidence, evidence_failures = tool.evaluate_timing_evidence(
                (log.read_text(encoding="utf-8"), "", ""),
                budget_contract,
                identity["map"],
                identity["backend"],
                identity["profile"],
            )
            assert evidence_failures == []
            result["budgetEvidence"] = evidence
        if role == "sp-capture":
            reference_path = (
                base
                / "savepaths"
                / contract["saveDir"]
                / contract["expected"]["saveReferenceScreenshot"]
            )
            preview_path = (
                base
                / "savepaths"
                / contract["saveDir"]
                / contract["expected"]["savePreview"]
            )
            comparison, failure = tool.save_preview_comparison(
                reference_path, preview_path
            )
            assert comparison is not None and failure is None
            result["savePreviewComparison"] = comparison
        results.append(result)
    live_sp_result = tool.evaluate_role(
        generated_plans["sp-capture"], base, 0, False
    )
    assert live_sp_result["status"] == "pass"
    assert live_sp_result["savePreviewComparison"] == next(
        result for result in results if result["role"] == "sp-capture"
    )["savePreviewComparison"]

    # Report replay is intentionally exercised through many independent
    # mutations below. Cache the expensive full-pixel TGA policy result by
    # exact bytes so the canonical 1280x720 fixture does not turn each
    # unrelated provenance mutation into another full image decode. Any image
    # byte mutation receives a different digest and is validated afresh.
    uncached_validate_tga = tool.validate_tga
    tga_validation_cache: dict[tuple[str, tuple[int, int] | None], str | None] = {}

    def cached_validate_tga(
        path: Path, expected_dimensions: tuple[int, int] | None = None
    ) -> str | None:
        key = (tool.sha256_file(path), expected_dimensions)
        if key not in tga_validation_cache:
            tga_validation_cache[key] = uncached_validate_tga(path, expected_dimensions)
        return tga_validation_cache[key]

    tool.validate_tga = cached_validate_tga
    uncached_save_preview_comparison = tool.save_preview_comparison
    preview_comparison_cache: dict[
        tuple[str, str], tuple[dict[str, object] | None, str | None]
    ] = {}

    def cached_save_preview_comparison(
        reference_path: Path, preview_path: Path
    ) -> tuple[dict[str, object] | None, str | None]:
        key = (
            tool.sha256_file(reference_path),
            tool.sha256_file(preview_path),
        )
        if key not in preview_comparison_cache:
            preview_comparison_cache[key] = uncached_save_preview_comparison(
                reference_path, preview_path
            )
        return preview_comparison_cache[key]

    tool.save_preview_comparison = cached_save_preview_comparison
    assert tool.verify_recorded_files(report, base, asset_root, runtime_dir) == []
    recorded_failures = tool.verify_recorded_files(report, base, asset_root)
    assert recorded_failures == [], recorded_failures

    wrong_display_contract = json.loads(json.dumps(report))
    wrong_display_contract["safety"]["displayContract"]["width"] = 1279
    assert any(
        "safety/window contract differs" in failure
        for failure in tool.verify_recorded_files(
            wrong_display_contract, base, asset_root, runtime_dir
        )
    )
    wrong_window_size = json.loads(json.dumps(report))
    wrong_window_size["safety"]["windowSize"]["width"] = 640
    assert any(
        "safety/window contract differs" in failure
        for failure in tool.verify_recorded_files(
            wrong_window_size, base, asset_root, runtime_dir
        )
    )

    for field, value in (
        ("contractId", "wrong-contract-v1"),
        ("path", "tools/validation/wrong.json"),
        ("sha256", "0" * 64),
    ):
        wrong_budget_binding = json.loads(json.dumps(report))
        wrong_budget_binding["budgetContract"][field] = value
        assert any(
            "budget contract binding differs" in failure
            for failure in tool.verify_recorded_files(
                wrong_budget_binding, base, asset_root, runtime_dir
            )
        )
    stock_sp_result = next(
        result for result in results if result["role"] == "sp-capture"
    )
    stock_sp_log_artifact = next(
        artifact
        for artifact in stock_sp_result["artifacts"]  # type: ignore[union-attr]
        if artifact["kind"] == "engineLog"
    )
    stock_sp_log = base / str(stock_sp_log_artifact["path"])
    stock_sp_log_text = stock_sp_log.read_text(encoding="utf-8")
    stock_sp_log.write_text(
        stock_sp_log_text.replace("backend=opengl", "backend=vulkan"),
        encoding="utf-8",
    )
    stock_sp_log_artifact.update(tool.file_record(stock_sp_log, base))
    mutated_marker_failures = tool.verify_recorded_files(
        report, base, asset_root, runtime_dir
    )
    assert any("timing backend" in failure for failure in mutated_marker_failures)
    assert any(
        "recorded renderer budget evidence differs" in failure
        for failure in mutated_marker_failures
    )
    stock_sp_log.write_text(stock_sp_log_text, encoding="utf-8")
    stock_sp_log_artifact.update(tool.file_record(stock_sp_log, base))
    assert tool.verify_recorded_files(report, base, asset_root, runtime_dir) == []

    stock_sp_log.write_text(
        stock_sp_log_text.replace(
            "MODE: -1, 1280 x 720 windowed",
            "MODE: -1, 1279 x 720 windowed",
        ),
        encoding="utf-8",
    )
    stock_sp_log_artifact.update(tool.file_record(stock_sp_log, base))
    assert any(
        "expected exact budget workload 1280x720" in failure
        for failure in tool.verify_recorded_files(report, base, asset_root, runtime_dir)
    )
    stock_sp_log.write_text(stock_sp_log_text, encoding="utf-8")
    stock_sp_log_artifact.update(tool.file_record(stock_sp_log, base))
    assert tool.verify_recorded_files(report, base, asset_root, runtime_dir) == []

    stock_sp_screenshot_artifact = next(
        artifact
        for artifact in stock_sp_result["artifacts"]  # type: ignore[union-attr]
        if artifact["kind"] == "screenshot"
    )
    stock_sp_screenshot = base / str(stock_sp_screenshot_artifact["path"])
    stock_sp_screenshot_bytes = stock_sp_screenshot.read_bytes()
    changed_screenshot = bytearray(stock_sp_screenshot_bytes)
    struct.pack_into("<H", changed_screenshot, 12, 1279)
    del changed_screenshot[-3 * 720 :]
    stock_sp_screenshot.write_bytes(changed_screenshot)
    stock_sp_screenshot_artifact.update(tool.file_record(stock_sp_screenshot, base))
    assert any(
        "TGA dimensions 1279x720 differ from expected 1280x720" in failure
        for failure in tool.verify_recorded_files(report, base, asset_root, runtime_dir)
    )
    stock_sp_screenshot.write_bytes(stock_sp_screenshot_bytes)
    stock_sp_screenshot_artifact.update(tool.file_record(stock_sp_screenshot, base))
    assert tool.verify_recorded_files(report, base, asset_root, runtime_dir) == []

    wrong_budget_measurement = json.loads(json.dumps(report))
    wrong_budget_measurement["results"][0]["budgetEvidence"]["measurement"]["cpu"]["p95Us"] = 1
    assert any(
        "recorded renderer budget evidence differs" in failure
        for failure in tool.verify_recorded_files(
            wrong_budget_measurement, base, asset_root, runtime_dir
        )
    )

    missing_expected_binding = json.loads(json.dumps(report))
    missing_expected_binding["expectedAssets"]["path"] = str(
        base / "missing-expected-assets.json"
    )
    assert any(
        "expected-assets manifest is missing" in failure
        for failure in tool.verify_recorded_files(
            missing_expected_binding, base, asset_root, runtime_dir
        )
    )
    wrong_expected_hash = json.loads(json.dumps(report))
    wrong_expected_hash["expectedAssets"]["sha256"] = "0" * 64
    assert any(
        "expected-assets manifest SHA-256 differs" in failure
        for failure in tool.verify_recorded_files(
            wrong_expected_hash, base, asset_root, runtime_dir
        )
    )
    unrelated_expected_path = base / "unrelated-expected-assets.json"
    unrelated_expected_path.write_text(
        json.dumps(
            {
                "schemaVersion": 1,
                "allowLinkedGameDirectories": False,
                "directoryViews": tool.asset_directory_views(asset_root),
                "stockPk4s": [],
                "looseFiles": [],
            }
        ),
        encoding="utf-8",
    )
    unrelated_expected = json.loads(json.dumps(report))
    unrelated_expected["expectedAssets"]["path"] = str(unrelated_expected_path.resolve())
    unrelated_expected["expectedAssets"]["sha256"] = tool.sha256_file(
        unrelated_expected_path
    )
    assert any(
        "inventory differs from the bound expected-assets manifest" in failure
        for failure in tool.verify_recorded_files(
            unrelated_expected, base, asset_root, runtime_dir
        )
    )
    dry_run_pass = json.loads(json.dumps(report))
    dry_run_pass["dryRun"] = True
    assert any(
        "dryRun=false" in failure
        for failure in tool.verify_recorded_files(dry_run_pass, base, asset_root, runtime_dir)
    )
    wrong_revision = json.loads(json.dumps(report))
    wrong_revision["git"]["revision"] = "0" * 40
    assert any(
        "not the current openQ4 HEAD" in failure
        for failure in tool.verify_recorded_files(
            wrong_revision, base, asset_root, runtime_dir
        )
    )
    for failure_field in tool.TOP_LEVEL_FAILURE_ARRAYS:
        recorded_failure = json.loads(json.dumps(report))
        recorded_failure[failure_field] = ["adversarial recorded failure"]
        assert any(
            f"{failure_field} field is nonempty" in failure
            for failure in tool.verify_recorded_files(
                recorded_failure, base, asset_root, runtime_dir
            )
        )
    false_collision_inventory = json.loads(json.dumps(report))
    false_collision_inventory["assets"]["retailPathCollisions"] = [
        {
            "path": "guis/fake.gui",
            "retailPk4s": ["q4base/pak001.pk4"],
            "openQ4OverlayPk4s": ["baseoq4/pak0.pk4"],
        }
    ]
    false_collision_inventory["assets"]["retailPathCollisionCount"] = 1
    false_collision_inventory["assets"]["retailPathNamespaceUntouched"] = False
    assert any(
        "collision inventory differs" in failure
        for failure in tool.verify_recorded_files(
            false_collision_inventory, base, asset_root, runtime_dir
        )
    )

    def mutated_launch(role: str, token: str, value: str) -> dict[str, object]:
        candidate = json.loads(json.dumps(report))
        role_plan = next(plan for plan in candidate["plan"] if plan["role"] == role)
        token_index = role_plan["arguments"].index(token)
        role_plan["arguments"][token_index + 1] = value
        return candidate

    for role, token, value, expected_failure in (
        ("sp-capture", "fs_game", "wronggame", "launch CVar fs_game differs"),
        ("sp-capture", "+map", "game/wrong", "launch command map differs"),
        ("sp-capture", "si_gameType", "DM", "launch CVar si_gameType differs"),
        ("mp-server", "+spawnServer", "mp/wrong", "launch command spawnServer differs"),
        ("mp-server", "net_serverDedicated", "1", "launch CVar net_serverDedicated differs"),
        ("mp-server", "si_gameType", "Tourney", "launch CVar si_gameType differs"),
        ("mp-client", "+connect", "127.0.0.1:1", "launch command connect differs"),
    ):
        assert any(
            expected_failure in failure
            for failure in tool.verify_recorded_files(
                mutated_launch(role, token, value), base, asset_root, runtime_dir
            )
        )

    mp_client_result = next(
        result for result in results if result["role"] == "mp-client"
    )
    mp_client_log_artifact = next(
        artifact
        for artifact in mp_client_result["artifacts"]  # type: ignore[union-attr]
        if artifact["kind"] == "engineLog"
    )
    mp_client_stdout_artifact = next(
        artifact
        for artifact in mp_client_result["artifacts"]  # type: ignore[union-attr]
        if artifact["kind"] == "processStdout"
    )
    mp_client_log = base / str(mp_client_log_artifact["path"])
    mp_client_stdout = base / str(mp_client_stdout_artifact["path"])
    mp_client_stdout.write_text(
        mp_client_log.read_text(encoding="utf-8"), encoding="utf-8"
    )
    mp_client_stdout_artifact.update(tool.file_record(mp_client_stdout, base))
    mirrored_live_result = tool.evaluate_role(
        generated_plans["mp-client"], base, 0, False
    )
    assert mirrored_live_result["status"] == "pass"
    assert tool.verify_recorded_files(report, base, asset_root, runtime_dir) == []
    mp_client_stdout.write_text("", encoding="utf-8")
    mp_client_stdout_artifact.update(tool.file_record(mp_client_stdout, base))
    save_preview_artifact = next(
        artifact
        for result in results
        for artifact in result["artifacts"]  # type: ignore[index]
        if artifact["kind"] == "savePreview"
    )
    save_preview_path = base / str(save_preview_artifact["path"])
    coherent_preview_bytes = save_preview_path.read_bytes()
    sp_capture_result = next(result for result in results if result["role"] == "sp-capture")
    coherent_comparison = sp_capture_result["savePreviewComparison"]
    sp_reference_artifact = next(
        artifact
        for artifact in sp_capture_result["artifacts"]  # type: ignore[union-attr]
        if artifact["kind"] == "saveReferenceScreenshot"
    )
    sp_reference_path = base / str(sp_reference_artifact["path"])
    preview_width, preview_height = tool.SP_SAVE_PREVIEW_DIMENSIONS
    write_rgb_tga(
        save_preview_path,
        preview_width,
        preview_height,
        recursive_strip_rgb(preview_width, preview_height),
    )
    save_preview_artifact.update(tool.file_record(save_preview_path, base))
    assert any(
        "save preview: TGA contains recursive scaled-strip repetition" in failure
        for failure in tool.verify_recorded_files(report, base, asset_root, runtime_dir)
    )
    save_preview_path.write_bytes(coherent_preview_bytes)
    save_preview_artifact.update(tool.file_record(save_preview_path, base))
    assert tool.verify_recorded_files(report, base, asset_root, runtime_dir) == []
    coherent_decoded = tool.decode_tga_rgb(coherent_preview_bytes)
    assert not isinstance(coherent_decoded, str)
    _, _, coherent_rgb = coherent_decoded
    write_rgb_tga(
        save_preview_path,
        preview_width,
        preview_height,
        severe_color_defect(coherent_rgb),
    )
    assert tool.validate_tga(save_preview_path, tool.SP_SAVE_PREVIEW_DIMENSIONS) is None
    save_preview_artifact.update(tool.file_record(save_preview_path, base))
    corrupted_comparison, comparison_failure = tool.save_preview_comparison(
        sp_reference_path, save_preview_path
    )
    assert corrupted_comparison is not None and comparison_failure is not None
    assert "exposure-compensated" in comparison_failure
    assert corrupted_comparison["lumaCorrelation"] >= tool.SP_SAVE_PREVIEW_MIN_LUMA_CORRELATION
    live_corrupted_result = tool.evaluate_role(
        generated_plans["sp-capture"], base, 0, False
    )
    assert live_corrupted_result["status"] == "fail"
    assert any(
        "save preview differs from same-state save reference" in failure
        for failure in live_corrupted_result["failures"]
    )
    sp_capture_result["savePreviewComparison"] = corrupted_comparison
    assert any(
        "save preview differs from same-state save reference" in failure
        for failure in tool.verify_recorded_files(report, base, asset_root, runtime_dir)
    )
    save_preview_path.write_bytes(coherent_preview_bytes)
    save_preview_artifact.update(tool.file_record(save_preview_path, base))
    sp_capture_result["savePreviewComparison"] = coherent_comparison
    assert tool.verify_recorded_files(report, base, asset_root, runtime_dir) == []
    missing_comparison = json.loads(json.dumps(report))
    del next(
        result for result in missing_comparison["results"] if result["role"] == "sp-capture"
    )["savePreviewComparison"]
    assert any(
        "recorded save-preview comparison metrics differ" in failure
        for failure in tool.verify_recorded_files(
            missing_comparison, base, asset_root, runtime_dir
        )
    )
    original_link_check = tool.is_link_or_junction
    for linked_path in (runtime_dir, runtime_dir.parent):
        tool.is_link_or_junction = lambda path, linked=linked_path: path == linked
        try:
            assert any(
                "runtime directory ancestry must not contain a link or junction" in failure
                for failure in tool.verify_recorded_files(report, base, asset_root)
            )
        finally:
            tool.is_link_or_junction = original_link_check
    wrong_runtime_root = json.loads(json.dumps(report))
    wrong_runtime_root["runtimeRoot"] = str(base / "wrong-runtime")
    assert any(
        "runtime root differs" in failure
        for failure in tool.verify_recorded_files(
            wrong_runtime_root, base, asset_root, runtime_dir
        )
    )
    missing_runtime_root = json.loads(json.dumps(report))
    del missing_runtime_root["runtimeRoot"]
    assert any(
        "does not record its runtime root" in failure
        for failure in tool.verify_recorded_files(
            missing_runtime_root, base, asset_root, runtime_dir
        )
    )
    runtime_executable.write_bytes(b"mutated client")
    assert any(
        "runtime file" in failure and "differs" in failure
        for failure in tool.verify_recorded_files(report, base, asset_root, runtime_dir)
    )
    runtime_executable.write_bytes(b"client")
    disabled_autojoin = json.loads(json.dumps(report))
    disabled_plan = next(
        plan for plan in disabled_autojoin["plan"] if plan["role"] == "mp-client"
    )
    disabled_index = disabled_plan["arguments"].index("ui_autoJoin")
    disabled_plan["arguments"][disabled_index + 1] = "0"
    assert any(
        "mp-client: launch CVar ui_autoJoin differs" in failure
        for failure in tool.verify_recorded_files(
            disabled_autojoin, base, asset_root, runtime_dir
        )
    )
    missing_autojoin = json.loads(json.dumps(report))
    missing_plan = next(
        plan for plan in missing_autojoin["plan"] if plan["role"] == "mp-server"
    )
    missing_index = missing_plan["arguments"].index("ui_autoJoin")
    del missing_plan["arguments"][missing_index - 1 : missing_index + 2]
    assert any(
        "mp-server: launch CVar ui_autoJoin differs" in failure
        for failure in tool.verify_recorded_files(
            missing_autojoin, base, asset_root, runtime_dir
        )
    )
    disabled_pure = json.loads(json.dumps(report))
    pure_plan = next(
        plan for plan in disabled_pure["plan"] if plan["role"] == "mp-server"
    )
    pure_index = pure_plan["arguments"].index("si_pure")
    pure_plan["arguments"][pure_index + 1] = "0"
    assert any(
        "mp-server: launch CVar si_pure differs" in failure
        for failure in tool.verify_recorded_files(
            disabled_pure, base, asset_root, runtime_dir
        )
    )
    empty_pass = dict(report)
    empty_pass["results"] = []
    assert any(
        "required role" in failure
        for failure in tool.verify_recorded_files(empty_pass, base, asset_root, runtime_dir)
    )
    missing_artifact = json.loads(json.dumps(report))
    missing_artifact["results"][0]["artifacts"] = [
        item
        for item in missing_artifact["results"][0]["artifacts"]
        if item["kind"] != "processStderr"
    ]
    assert any(
        "required artifact kinds differ" in failure
        for failure in tool.verify_recorded_files(
            missing_artifact, base, asset_root, runtime_dir
        )
    )
    stdout_artifact = next(
        item for item in results[0]["artifacts"] if item["kind"] == "processStdout"  # type: ignore[union-attr]
    )
    artifact = base / str(stdout_artifact["path"])
    artifact.write_text("tampered stream", encoding="utf-8")
    assert any(
        "differs" in failure
        for failure in tool.verify_recorded_files(report, base, asset_root, runtime_dir)
    )


def main() -> None:
    tool = load_tool()
    temp_parent = ROOT / ".tmp" / "stock-runtime"
    temp_parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="stock-baseline-test-", dir=temp_parent) as temp:
        base = Path(temp)
        test_asset_inventory_and_comparison(tool, base)
        test_plan_is_windowed_and_engine_only(tool, base)
        test_diagnostic_authority_and_shared_mp_deadline(tool, base)
        test_save_preview_coherence(tool, base)
        test_tga_and_report_verification(tool, base)
    print("stock_asset_baseline: ok")


if __name__ == "__main__":
    main()
