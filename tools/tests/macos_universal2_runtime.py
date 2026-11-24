#!/usr/bin/env python3
"""Regression checks for the macOS universal2 runtime-module contract."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(source: str, needle: str, context: str) -> None:
    if needle not in source:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(source: str, needle: str, context: str) -> None:
    if needle in source:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def validate_loader_fallback() -> None:
    common = read("src/framework/Common.cpp")
    load_start = common.index("void idCommonLocal::LoadGameDLL")
    load_end = common.index("void idCommonLocal::UnloadGameDLL", load_start)
    load = common[load_start:load_end]

    for token in (
        "openQ4_BuildGameModuleBinaryNameForArch",
        "OPENQ4_MODULE_ARCH_TAG",
        'openQ4_BuildGameModuleBinaryNameForArch( "universal2", universalGameModuleBinary );',
        "fileSystem->FindDLL( universalGameModuleBinary, dllPath, true )",
        "selectedModuleBinary = universalGameModuleBinary",
        "#if defined( MACOS_X ) || defined( __APPLE__ )",
    ):
        require(common, token, "macOS universal2 game-module loader")

    preferred_lookup = load.index("fileSystem->FindDLL( selectedModuleBinary, dllPath, true )")
    fallback_guard = load.index("if ( !dllPath[ 0 ] )", preferred_lookup)
    universal_lookup = load.index("fileSystem->FindDLL( universalGameModuleBinary, dllPath, true )")
    fatal_guard = load.index("if ( !dllPath[ 0 ] )", universal_lookup)
    if not preferred_lookup < fallback_guard < universal_lookup < fatal_guard:
        raise AssertionError("universal2 lookup must follow thin lookup and precede the fatal missing-module guard")

    # one module serves both logical modules, so the name no longer depends on the mode
    reject(load, "openQ4_BuildGameModuleBinaryNameForArch( gameModuleBaseName", "mode-specific game module name construction")


def validate_bundle_integrity_fallback() -> None:
    compat = read("src/sys/macos/macos_compat.mm")
    start = compat.index("static void Sys_CollectMacOSGameModuleIssues")
    end = compat.index("static bool Sys_MacOSAppBundleHasEmbeddedRuntimeMarker", start)
    contract = compat[start:end]

    for token in (
        "Sys_CollectMacOSGameModuleIssues",
        '"game_%s.dylib", arch',
        "Sys_MacOSPackageRuntimeArchSuffix()",
        'Sys_CollectMacOSGameModuleIssues( frameworkDirectory, "universal2"',
        "architectureModuleIssues.Length() == 0",
        "universalModuleIssues.Length() == 0",
        "architecture-specific game module unavailable",
        "universal2 game module unavailable",
        "Sys_RequireMacOSPackageRootExecutable",
    ):
        require(contract, token, "self-contained app universal2 integrity fallback")

    arch_check = contract.index("Sys_MacOSPackageRuntimeArchSuffix()")
    universal_check = contract.index('frameworkDirectory, "universal2"')
    if arch_check >= universal_check:
        raise AssertionError("self-contained app validation must prefer architecture-specific modules")


def validate_module_search_diagnostics() -> None:
    filesystem = read("src/framework/FileSystem.cpp")
    common = read("src/framework/Common.cpp")
    start = filesystem.index("static idFile *FS_OpenGameModuleFromExeDir")
    end = filesystem.index("void idFileSystemLocal::ClearDirCache", start)
    contract = filesystem[start:end]

    for token in (
        "FS_AppendGameModuleSearchPath",
        "trustedModuleRoots",
        "attemptedModulePaths",
        "Game module search failed:",
        "Sys_GetGameModuleRootDirectory",
        "FS_AppendGameModuleSearchPath( attemptedModulePaths, moduleSearchRoots[i], moduleGameDir, dllName );",
        "FS_AppendGameModuleSearchPath( attemptedModulePaths, moduleSearchRoots[i], OPENQ4_GAMEDIR, dllName );",
        "FS_AppendGameModuleSearchPath( attemptedModulePaths, moduleSearchRoots[i], NULL, dllName );",
    ):
        require(contract, token, "trusted game-module search diagnostics")

    diagnostic = contract.index("Game module search failed:")
    flat_lookup = contract.index("FS_AppendGameModuleSearchPath( attemptedModulePaths, moduleSearchRoots[i], NULL, dllName );")
    if diagnostic <= flat_lookup:
        raise AssertionError("missing-module diagnostic must follow all trusted module lookup attempts")

    for token in (
        "Game module load failed:",
        "inspect the preceding platform loader error.",
        "gameDLL = sys->DLL_Load( dllPath );",
        "couldn't load game dynamic library",
    ):
        require(common, token, "game-module dynamic-loader failure diagnostic")

    load_attempt = common.index("gameDLL = sys->DLL_Load( dllPath );")
    load_diagnostic = common.index("Game module load failed:", load_attempt)
    load_fatal = common.index("couldn't load game dynamic library", load_diagnostic)
    if not load_attempt < load_diagnostic < load_fatal:
        raise AssertionError("game-module load diagnostic must precede the generic fatal error")


def validate_docs_and_wiring() -> None:
    design = read("docs/dev/macos-universal2-design.md")
    matrix = read("docs/dev/macos-support-matrix-policy.md")
    plan = read("docs/dev/archive/2026-10-18/plan/2026-06-30-macos-compatibility-support.md")
    release_completion = read("docs/dev/release-completion.md")
    release_notes = read("docs/dev/releases/v0.8.1.md")
    validator = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")

    for token in (
        "# macOS Universal2 Design",
        "tools/build/assemble_macos_universal2.py",
        "game_universal2.dylib",
        "lipo -create",
        "exact slice set `arm64 x86_64`",
        "sign nested modules inside-out",
        "Publishing `macos-universal2` requires",
    ):
        require(design, token, "macOS universal2 design")

    require(matrix, "macos-universal2-design.md", "macOS matrix universal2 design link")
    require(plan, "universal2 runtime module fallback", "macOS plan universal2 runtime status")
    require(plan, "fail-closed universal2 merge/assembly job", "macOS plan universal2 assembly status")
    require(release_completion, "universal2 runtime groundwork", "release completion universal2 entry")
    require(release_notes, "Universal2 runtime groundwork", "curated release notes universal2 entry")

    for source, context in (
        (validator, "local validation runner"),
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
    ):
        require(source, "macos_universal2_runtime.py", context)

    # Releases are arm64-only, so universal2 assembly runs in the manual
    # candidate lane rather than on every pull request.
    require(commit, "Record macOS ARM64 thin-build provenance", "commit validation arm64 thin provenance")
    reject(commit, "macos-universal2:", "commit validation universal2 scope")
    candidate = read(".github/workflows/macos-universal2-candidate.yml")
    for token in (
        "assemble_macos_universal2.py record",
        "assemble_macos_universal2.py assemble",
        "--arch universal2",
        "python tools/tests/macos_dedicated_server_smoke.py",
    ):
        require(candidate, token, "manual universal2 assembly workflow")

    package = read("tools/build/package_nightly.py")
    manual_release = read(".github/workflows/manual-release.yml")
    require(package, "macos_expected_lipo_arches(arch)", "exact universal2 package slice validation")
    require(package, '"universal2": frozenset(("arm64", "x86_64"))', "universal2 package slice mapping")
    for token in (
        'universal2) expected_lipo_arches="arm64 x86_64"',
        "actual_lipo_arches",
        'otool -arch "${macho_arch}" -D',
        'otool -arch "${macho_arch}" -L',
    ):
        require(manual_release, token, "manual-release exact Mach-O slice validator")


def main() -> None:
    validate_loader_fallback()
    validate_bundle_integrity_fallback()
    validate_module_search_diagnostics()
    validate_docs_and_wiring()
    print("macos_universal2_runtime: ok")


if __name__ == "__main__":
    main()
