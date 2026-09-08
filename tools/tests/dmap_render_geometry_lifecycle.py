#!/usr/bin/env python3
"""Regression contract for engine-side render-geometry allocator lifetime."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TEST_PATH = "tools/tests/dmap_render_geometry_lifecycle.py"


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_order(haystack: str, needles: tuple[str, ...], context: str) -> None:
    previous = -1
    for needle in needles:
        position = haystack.find(needle, previous + 1)
        if position == -1:
            raise AssertionError(f"Missing {needle!r} in {context}")
        if position <= previous:
            raise AssertionError(f"Expected ordered snippets in {context}: {needles!r}")
        previous = position


def braced_body(source: str, marker: str, context: str) -> str:
    start = source.find(marker)
    if start == -1:
        raise AssertionError(f"Missing {marker!r} in {context}")

    opening = source.find("{", start + len(marker))
    if opening == -1:
        raise AssertionError(f"Missing opening brace after {marker!r} in {context}")

    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"Could not find closing brace for {marker!r} in {context}")


def validate_allocator_lifecycle() -> None:
    source = read("src/render_geo/RenderGeometryTriSurf.cpp")
    require(source, "static bool\t\t\ttriSurfDataInitialized;", "triangle allocator state")

    init = braced_body(source, "void R_InitTriSurfData( void )", "triangle allocator initialization")
    require_order(
        init,
        (
            "if ( triSurfDataInitialized )",
            "return;",
            "triVertexAllocator.Init();",
            "triIndexAllocator.Init();",
            "triSurfDataInitialized = true;",
        ),
        "triangle allocator initialization",
    )

    shutdown = braced_body(source, "void R_ShutdownTriSurfData( void )", "triangle allocator shutdown")
    require_order(
        shutdown,
        (
            "if ( !triSurfDataInitialized )",
            "return;",
            "triSurfDataInitialized = false;",
            "R_StaticFree( silEdges );",
            "silEdges = NULL;",
            "triVertexAllocator.Shutdown();",
            "triIndexAllocator.Shutdown();",
        ),
        "triangle allocator shutdown",
    )


def validate_engine_tool_lifetime() -> None:
    common = read("src/framework/Common.cpp")
    require(common, '#include "../render_geo/RenderGeometry.h"', "Common render-geometry API")

    init = braced_body(common, "void idCommonLocal::Init(", "Common initialization")
    require_order(
        init,
        ("InitSIMD();", "InitCommands();", "InitGame();"),
        "Common initialization",
    )
    if "R_InitTriSurfData();" in init:
        raise AssertionError("Common must not initialize render geometry before renderer allocation hooks")

    init_game = braced_body(common, "void idCommonLocal::InitGame( void )", "game initialization")
    require_order(
        init_game,
        ("renderSystem->Init();", "R_InitTriSurfData();", "cmdSystem->ExecuteCommandBuffer();"),
        "engine-side render-geometry initialization",
    )
    if init_game.count("R_InitTriSurfData();") != 1:
        raise AssertionError("game initialization must initialize the engine/tool TriSurf copy exactly once")

    shutdown = braced_body(common, "void idCommonLocal::Shutdown( void )", "Common shutdown")
    require_order(
        shutdown,
        ("ShutdownGame( false );", "R_ShutdownTriSurfData();", "idLib::ShutDown();"),
        "engine-side render-geometry shutdown",
    )

    renderer = read("src/renderer/RenderSystem_init.cpp")
    renderer_init = braced_body(renderer, "void idRenderSystemLocal::Init( void )", "renderer initialization")
    require_order(
        renderer_init,
        ("R_InstallImageToolsHooks();", "R_InstallRenderGeoHooks();", "R_InitTriSurfData();"),
        "renderer-owned render-geometry initialization",
    )
    if renderer_init.count("R_InitTriSurfData();") != 1:
        raise AssertionError("renderer initialization must initialize its TriSurf copy exactly once")

    renderer_shutdown = braced_body(renderer, "void idRenderSystemLocal::Shutdown( void )", "renderer shutdown")
    if renderer_shutdown.count("R_ShutdownTriSurfData();") != 1:
        raise AssertionError("renderer shutdown must release its TriSurf copy exactly once")


def validate_map_resolution_contract() -> None:
    source = read("src/tools/compilers/dmap/map.cpp")
    load = braced_body(source, "bool LoadDMapFile( const char *filename )", "dmap map loading")
    require_order(
        load,
        (
            "dmapGlobals.dmapFile->Parse(filename)",
            "dmapGlobals.dmapFile->Resolve();",
            "dmapGlobals.dmapFile->GetNumEntities()",
            "ProcessMapEntity( dmapGlobals.dmapFile->GetEntity(i) );",
        ),
        "dmap func_group resolution",
    )
    if load.count("dmapGlobals.dmapFile->Resolve();") != 1:
        raise AssertionError("dmap must resolve editable func_group entities exactly once before compiling geometry")


def validate_build_and_ci_wiring() -> None:
    meson = read("meson.build")
    require(meson, "client_link_with = [bse_library, imagetools_library, render_geo_library]", "client render-geometry link")
    require(meson, "link_with: [renderer_idlib_library, imagetools_library, render_geo_library]", "renderer render-geometry link")

    validator = read("tools/validation/openq4_validate.py")
    if validator.count("dmap_render_geometry_lifecycle.py") != 1:
        raise AssertionError("Local validation must register the dmap render-geometry test exactly once")

    for workflow_path in (
        ".github/workflows/commit-validation.yml",
        ".github/workflows/push-verification.yml",
    ):
        workflow = read(workflow_path)
        if workflow.count(TEST_PATH) != 2:
            raise AssertionError(f"{workflow_path} must compile and run {TEST_PATH}")
        require(workflow, f"python {TEST_PATH}", workflow_path)


def validate_compiled_map_metadata() -> None:
    output = read("src/tools/compilers/dmap/output.cpp")
    require(output, '"%u\\n", dmapGlobals.dmapFile->GetGeometryCRC()', "compiled geometry checksum")
    if "1105723392" in output or "Fake CRC" in output:
        raise AssertionError("dmap must not mark newly compiled geometry with a placeholder checksum")
    dmap = read("src/tools/compilers/dmap/dmap.cpp")
    require(dmap, "AAS compilation is not available", "missing navigation compiler diagnostic")


def main() -> None:
    validate_allocator_lifecycle()
    validate_engine_tool_lifetime()
    validate_map_resolution_contract()
    validate_build_and_ci_wiring()
    validate_compiled_map_metadata()
    print("dmap_render_geometry_lifecycle: ok")


if __name__ == "__main__":
    main()
