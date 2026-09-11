#!/usr/bin/env python3
"""Guard the in-engine Quake 4 AAS navigation compiler and its dmap wiring."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TEST_PATH = "tools/tests/aas_compiler_contract.py"

COMPILER_SOURCES = (
    "src/tools/compilers/aas/AASBuild.cpp",
    "src/tools/compilers/aas/AASBuild_file.cpp",
    "src/tools/compilers/aas/AASBuild_gravity.cpp",
    "src/tools/compilers/aas/AASBuild_ledge.cpp",
    "src/tools/compilers/aas/AASBuild_local.h",
    "src/tools/compilers/aas/AASBuild_merge.cpp",
    "src/tools/compilers/aas/AASCluster.cpp",
    "src/tools/compilers/aas/AASCluster.h",
    "src/tools/compilers/aas/AASReach.cpp",
    "src/tools/compilers/aas/AASReach.h",
    "src/tools/compilers/aas/Brush.cpp",
    "src/tools/compilers/aas/Brush.h",
    "src/tools/compilers/aas/BrushBSP.cpp",
    "src/tools/compilers/aas/BrushBSP.h",
)


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_bytes().decode("windows-1252")


def function_body(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing {signature!r} in {context}")
    opening = source.find("{", start + len(signature))
    if opening < 0:
        raise AssertionError(f"Missing body for {signature!r} in {context}")

    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"Unterminated body for {signature!r} in {context}")


def require(source: str, token: str, context: str) -> None:
    if token not in source:
        raise AssertionError(f"Missing {token!r} in {context}")


def forbid(source: str, token: str, context: str) -> None:
    if token in source:
        raise AssertionError(f"Unexpected {token!r} in {context}")


def validate_compiler_sources_present() -> None:
    for relative_path in COMPILER_SOURCES:
        path = ROOT / relative_path
        if not path.is_file():
            raise AssertionError(f"Missing AAS compiler source {relative_path}")

    sources = read("tools/build/meson_sources.py")
    require(sources, '"tools/compilers/aas/*.cpp",', "engine source globs")


def validate_map_fidelity() -> None:
    build = read("src/tools/compilers/aas/AASBuild.cpp")

    # Quake 4 keeps func_group entities in the editable .map; the navigation mesh
    # has to see the same world brushes dmap and the runtime map loader do.
    require(build, "mapFile->Resolve();", "AAS map loading")

    # The Quake 4 .proc header carries a quoted version and the geometry CRC
    # between the file id and the first section.
    proc = function_body(
        build,
        "bool idAASBuild::LoadProcBSP( const char *name, unsigned int minFileTime )",
        "AAS .proc loader",
    )
    require(proc, "PROC_FILE_ID", "AAS .proc loader")
    require(proc, "is missing version", "AAS .proc loader")
    require(proc, "has no map file CRC", "AAS .proc loader")


def validate_dummy_file_behavior() -> None:
    build = read("src/tools/compilers/aas/AASBuild.cpp")

    # A map with nothing that uses an AAS type still needs a placeholder file, or
    # idAASLocal::Init() reports missing navigation on every load instead of
    # recognising a dummy through idAASFile::IsDummyFile().
    require(
        build,
        "bool idAASBuild::WriteDummyFile( const idStr &fileName, unsigned int mapFileCRC )",
        "AAS dummy writer",
    )
    body = function_body(
        build,
        "bool idAASBuild::Build( const idStr &fileName, const idAASSettings *settings )",
        "AAS build",
    )
    require(body, "WriteDummyFile( fileName, mapFile->GetGeometryCRC() );", "AAS build")

    optimize = function_body(
        read("src/aas/AASFile_optimize.cpp"),
        "void idAASFileLocal::Optimize( void )",
        "AAS optimizer",
    )
    # SetNum() leaves the dummy elements uninitialised; without these the compiler
    # writes raw allocator contents as edge 0 and face 0.
    require(optimize, "memset( &newEdges[0], 0, sizeof( newEdges[0] ) );", "AAS optimizer")
    require(optimize, "memset( &newFaces[0], 0, sizeof( newFaces[0] ) );", "AAS optimizer")


def validate_command_wiring() -> None:
    public = read("src/tools/compilers/compiler_public.h")
    require(public, "bool RunAASForMap( const idStr &mapName, const idCmdArgs *optionArgs );", "compiler_public.h")

    build = read("src/tools/compilers/aas/AASBuild.cpp")
    require(build, "bool RunAASForMap( const idStr &mapName, const idCmdArgs *optionArgs ) {", "AAS command core")
    # dmap must be able to compile navigation without a loaded game module.
    require(build, 'declManager->FindType( DECL_ENTITYDEF, "aas_types", false )', "AAS type discovery")
    forbid(build, "gameEdit->FindEntityDefDict", "AAS type discovery")

    common = read("src/framework/Common.cpp")
    dmap_registration = common.find('cmdSystem->AddCommand("dmap", Dmap_f,')
    if dmap_registration < 0:
        raise AssertionError("Missing dmap command registration in Common.cpp")
    tools_only = common.find("#ifdef ID_ALLOW_TOOLS", dmap_registration)
    if tools_only < 0:
        raise AssertionError("Expected an ID_ALLOW_TOOLS block after the dmap registration")

    # Map authors get the AAS compiler from any build that has dmap, not only
    # from an ID_ALLOW_TOOLS editor build.
    for command in ("runAAS", "runAASDir", "runReach"):
        registration = common.find(f'cmdSystem->AddCommand( "{command}", ')
        if registration < 0:
            raise AssertionError(f"Missing {command} command registration in Common.cpp")
        if not dmap_registration < registration < tools_only:
            raise AssertionError(f"{command} must register with dmap, outside ID_ALLOW_TOOLS")


def validate_dmap_runs_aas() -> None:
    dmap = read("src/tools/compilers/dmap/dmap.cpp")
    require(dmap, "RunAASForMap( passedName, &args )", "dmap AAS pass")
    forbid(dmap, "AAS compilation is not available", "dmap AAS pass")


def validate_output_paths_are_reported() -> None:
    # dmap writes beside the engine rather than beside the .map it read, so every
    # compiled output has to name the file it actually produced.
    require(read("src/tools/compilers/dmap/output.cpp"), "procFile->GetFullPath()", ".proc writer")
    require(read("src/cm/CollisionModel_files.cpp"), "fp->GetFullPath()", ".cm writer")
    require(read("src/aas/AASFile.cpp"), "aasFile->GetFullPath()", "AAS writer")


def validate_ci_wiring() -> None:
    validator = read("tools/validation/openq4_validate.py")
    if validator.count("aas_compiler_contract.py") != 1:
        raise AssertionError("Local validation must register the AAS compiler contract exactly once")

    for workflow_path in (
        ".github/workflows/commit-validation.yml",
        ".github/workflows/push-verification.yml",
    ):
        workflow = read(workflow_path)
        if workflow.count(TEST_PATH) != 2:
            raise AssertionError(f"{workflow_path} must compile and run {TEST_PATH}")
        require(workflow, f"python {TEST_PATH}", workflow_path)


def main() -> None:
    validate_compiler_sources_present()
    validate_map_fidelity()
    validate_dummy_file_behavior()
    validate_command_wiring()
    validate_dmap_runs_aas()
    validate_output_paths_are_reported()
    validate_ci_wiring()
    print("aas_compiler_contract: ok")


if __name__ == "__main__":
    main()
