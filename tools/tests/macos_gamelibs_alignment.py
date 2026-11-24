#!/usr/bin/env python3
"""Static checks for Phase 8 macOS GameLibs alignment."""

from __future__ import annotations

import hashlib
import os
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)).resolve()

# Engine-interface headers that openQ4-game carries as copies. The engine is
# authoritative: any content drift changes the effective virtual layouts the
# standalone-built game modules are compiled against, which breaks the ABI at
# the engine/game boundary even when GAME_API_VERSION still matches.
ENGINE_INTERFACE_HEADERS = (
    "src/framework/BuildDefines.h",
    "src/framework/BuildVersion.h",
    "src/framework/CVarSystem.h",
    "src/framework/CmdSystem.h",
    "src/framework/Common.h",
    "src/framework/DeclPDA.h",
    "src/framework/DeclPlayerModel.h",
    "src/framework/File.h",
    "src/framework/FileSystem.h",
    "src/framework/UsercmdGen.h",
    "src/framework/async/NetworkSystem.h",
    "src/framework/declAF.h",
    "src/framework/declEntityDef.h",
    "src/framework/declLipSync.h",
    "src/framework/declManager.h",
    "src/framework/declMatType.h",
    "src/framework/declPlayback.h",
    "src/framework/declSkin.h",
    "src/framework/declTable.h",
    "src/framework/licensee.h",
    "src/imagetools/ImageContentIdentity.h",
    "src/renderer/Cinematic.h",
    "src/renderer/ImageOpts.h",
    "src/renderer/Material.h",
    "src/renderer/Model.h",
    "src/renderer/ModelManager.h",
    "src/renderer/RenderSystem.h",
    "src/renderer/RenderWorld.h",
    "src/renderer/RendererCaps.h",
    "src/renderer/RendererConsumedPolicy.h",
    "src/sound/sound.h",
    "src/sys/sys_public.h",
    "src/ui/ListGUI.h",
    "src/ui/UserInterface.h",
)


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def read_game_libs(relative_path: str) -> str:
    path = GAME_LIBS_ROOT / relative_path
    if not path.is_file():
        raise AssertionError(f"openQ4-game file not found: {path}")
    return path.read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def normalized_content_hash(path: Path) -> str:
    # Normalize line endings so checkout-time autocrlf differences between the
    # two repositories do not mask or fake drift; everything else must match.
    data = path.read_bytes().replace(b"\r\n", b"\n")
    return hashlib.sha256(data).hexdigest()


def validate_engine_interface_header_parity() -> None:
    # There is one engine interface. Games must never acquire stale copies.
    for relative_path in ENGINE_INTERFACE_HEADERS:
        assert (ROOT / relative_path).is_file(), relative_path
    for tree in ('game',):
        assert not (ROOT / 'src' / tree / 'framework').exists()
    meson=read('meson.build')
    require(meson,"src_include_dir = include_directories('src')",'shared canonical engine interface')
    require(meson,'game_idlib_include_dirs = [root_include_dir, src_include_dir]','game interface includes')
    # one game module, so no second multiplayer-flavoured interface include set
    reject(meson,'game_idlib_mp_include_dirs','retired MP interface includes')


def validate_companion_ci_revision() -> None:
    for name in ('commit-validation.yml','push-verification.yml','linux-arm64-cross.yml',
                 'macos-debug.yml','macos-sanitizer.yml','manual-release.yml','macos-universal2-candidate.yml'):
        source=read(f'.github/workflows/{name}')
        reject(source,'OPENQ4_GAMELIBS_SHA','retired companion pin')
        reject(source,'openQ4-game.git','retired companion fetch')
        assert not re.search(r'^      openq4_game_ref:\s*$',source,re.MULTILINE), 'retired companion revision input'
        require(source,'actions/checkout@','canonical source checkout')


def validate_companion_boundary() -> None:
    meson=read('meson.build')
    for tree in ('game',):
        assert (ROOT / 'src' / tree / 'Game_local.h').is_file()
    require(meson,'game_libs_repo_root = meson.project_source_root()','in-tree source ownership')
    reject(meson,'stage_gamelibs.py','direct compiler inputs')
    require(meson,"output: 'openq4_game_sources.json'",'canonical source inventory')
    inventory=read('tools/build/game_source_inventory.py')
    for field in ('projectGitCommit','gameLibsGitCommit','projectGitDirty','gameLibsGitDirty','sourceDigest'):
        require(inventory,field,'single-checkout provenance')


def validate_companion_macos_contract() -> None:
    meson=read('meson.build')
    modules=read('content/baseoq4/meson.build')
    for token in ("binary_arch = 'arm64'", "binary_arch = 'x64'",
                  "game_binary_name = 'game_' + binary_arch"):
        require(meson,token,'in-tree macOS module architecture')
    for token in ("name_suffix: 'dylib'",
                  "'-Wl,-install_name,@loader_path/' + game_binary_name + '.dylib'"):
        require(modules,token,'in-tree macOS module install name')
    require(read('.github/workflows/commit-validation.yml'),'macos-15','in-tree macOS CI coverage')
    require(read('.github/workflows/push-verification.yml'),'macos-15-intel','in-tree macOS Intel CI coverage')
    validator=read('tools/validation/openq4_validate.py')
    require(validator,'lipo','staged Mach-O architecture validation')
    require(read('tools/build/package_nightly.py'),'macos_otool_install_name','packaged Mach-O install name validation')
    require(read('tools/build/package_nightly.py'),'macos_otool_dependencies','packaged Mach-O dependency validation')


def validate_game_module_symbol_discipline() -> None:
    """Issue #90: darwin was the only host that let a game module export its
    whole symbol table, and the split SP/MP modules once shared one
    single-player-flavoured idlib archive even though GAME_MPAPI changed which
    Game_local.h every idlib translation unit compiled against. One game module
    now links the one game idlib archive, so no second flavour may return."""

    engine_meson = read("meson.build")
    module_meson = read("content/baseoq4/meson.build")
    engine_exports = read("tools/build/darwin_game_module.exp")

    allowed_exports = {"_GetGameAPI", "_openQ4_Mem_GetModuleStats"}
    actual_exports = set()
    for line in engine_exports.splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            actual_exports.add(stripped)
    if actual_exports != allowed_exports:
        raise AssertionError(f"darwin game module must export only its API and per-module memory counters: {actual_exports}")
    require(read("src/framework/Common.cpp"), "Sys_DLL_GetProcAddress( gameDLL, MEM_MODULE_STATS_ENTRY_POINT )", "game memory counter lookup is scoped to its module handle")

    for token in (
        "game_idlib_library = static_library(",
        "'openq4_game_idlib',",
        "darwin_game_module_export_list = files('tools/build/darwin_game_module.exp')",
        "'-Wl,-exported_symbols_list,' + darwin_game_module_export_list[0].full_path()",
    ):
        require(engine_meson, token, "engine game idlib and darwin export list")
    for token in ("game_idlib_library_mp", "'openq4_game_idlib_mp',", "game_idlib_mp_include_dirs"):
        reject(engine_meson, token, "retired multiplayer-flavoured game idlib")

    reject(module_meson, "game_idlib_library_mp", "retired multiplayer-flavoured game idlib")
    if module_meson.count("link_with: game_idlib_library,") != 3:
        raise AssertionError("every game module target (Windows, darwin, other) must link the game idlib archive")
    if module_meson.count("gnu_symbol_visibility: 'hidden',") != 2:
        raise AssertionError("darwin and linux game modules must both hide non-exported symbols")

    # The engine and the game module share compiler flags and support code.
    reject(engine_meson,"common_defines += ['_DEBUG']",'Clang game/engine layout parity')
    require(engine_meson,'game_common_cpp_args = shared_cpp_args +','shared game/engine flags')

    lib_header = read("src/idlib/Lib.h")
    require(
        lib_header,
        "#if defined( _DEBUG ) || defined( OPENQ4_ENABLE_IDLIB_ASSERTS )",
        "idlib assert gate reachable on Clang",
    )
    require(read("meson_options.txt"), "'idlib_asserts',", "idlib assert build option")


def validate_build_string_contract() -> None:
    sys_public = read("src/sys/sys_public.h")
    reject(sys_public, "MacOSX-universal", "macOS build string contract")
    for token in (
        '"macos-ppc"',
        '"macos-x86"',
        '"macos-x64"',
        '"macos-arm64"',
        '"macos-unknown"',
    ):
        require(sys_public, token, "macOS architecture-specific build string contract")


def validate_metadata_contract() -> None:
    packager = read("tools/build/package_nightly.py")
    collector = read("tools/macos/collect_macos_support_info.sh")
    signoff = read("tools/macos/guest/openq4-macos-sync-build-test.sh")
    validator = read("tools/macos/validate_signoff_archive.py")
    signoff_fixture = read("tools/tests/macos_signoff_archive.py")

    for token in (
        "VERSION_REPOSITORY_METADATA_KEYS",
        "GAMELIBS_STAGE_MANIFEST_NAME",
        "collect_package_repository_metadata",
        "read_staged_repository_metadata",
        "openq4_commit",
        "openq4_dirty",
        "openq4_game_commit",
        "openq4_game_dirty",
        "repository_metadata=repository_metadata",
    ):
        require(packager, token, "macOS package VERSION metadata contract")

    for token in (
        "package/build-metadata.txt",
        "package/app-VERSION.txt",
        "openq4_commit",
        "openq4_game_commit",
        "game_*.dylib",
    ):
        require(collector, token, "macOS support collector build metadata contract")

    for token in (
        "git_commit_or_unavailable",
        "git_dirty_or_unavailable",
        "- openQ4 commit:",
        "- openQ4 dirty:",
        "- \\`openQ4-game\\` commit:",
        "- \\`openQ4-game\\` dirty:",
    ):
        require(signoff, token, "macOS signoff provenance contract")

    for token in (
        "- openQ4 commit:",
        "- openQ4 dirty:",
        "- `openQ4-game` commit:",
        "- `openQ4-game` dirty:",
    ):
        require(validator, token, "macOS signoff archive provenance validator")
        require(signoff_fixture, token, "macOS signoff archive test fixture")


def validate_phase8_docs() -> None:
    plan = read("docs/dev/archive/2026-10-18/plans/2026-06-30-apple-support-no-macos-access.md")
    compatibility_plan = read("docs/dev/archive/2026-10-18/plan/2026-06-30-macos-compatibility-support.md")
    release_completion = read("docs/dev/release-completion.md")
    release_notes = read("docs/dev/releases/v0.6.5.md")
    support_data = read("docs/user/macos-support-data.md")
    signoff_evidence = read("docs/dev/macos-signoff-evidence.md")

    for token in (
        "## Phase 8: Align `openQ4-game`",
        "Phase 8 implementation status",
        "[x] Keep `openQ4-game` Darwin/Clang support",
        "[x] Add or keep static tests for `.dylib` names",
        "[x] Add or keep static tests for `@loader_path/<module>.dylib` install names",
        "[x] Make build strings report the actual macOS architecture",
        "[x] Add ABI/static checks for ARM64-sensitive game allocations",
        "[x] Record both openQ4 and `openQ4-game` commits",
    ):
        require(plan, token, "Phase 8 plan checklist")

    for token in (
        "Experimental macOS GameLibs alignment",
        "openQ4 and `openQ4-game` commits",
        "ARM64-sensitive allocation",
    ):
        require(release_completion, token, "release completion notes")
        require(release_notes, token, "curated release notes")

    for token in (
        "package/build-metadata.txt",
        "openq4_commit",
        "openq4_game_commit",
    ):
        require(support_data, token, "macOS support data guide")

    require(signoff_evidence, "openQ4 commit and game-source inventory", "macOS signoff evidence index")
    require(signoff_evidence, "both commit fields to the same openQ4 SHA", "macOS signoff evidence index")

    for token in (
        "[x] Keep openQ4 staged builds as the release source of truth for game-module validation.",
        "[x] Add a cross-repo validation note to the macOS evidence index",
        "[x] Verify `@loader_path/game-*.dylib` install names in release validation",
        "[x] Add `../openQ4-game` macOS x86_64 CI for the experimental Intel corridor.",
        "[x] Keep `openQ4-game` README support claims aligned with openQ4 platform support docs.",
        "[x] Add a small scripted check in openQ4",
    ):
        require(compatibility_plan, token, "MAC-007 compatibility-plan status")


def validate_wiring() -> None:
    validator = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")
    macos_debug = read(".github/workflows/macos-debug.yml")
    companion_workflow = read_game_libs(".github/workflows/commit-validation.yml")

    require(validator, "macos_gamelibs_alignment.py", "local openQ4 validation wiring")

    for workflow, context in (
        (commit, "openQ4 commit validation workflow"),
        (push, "openQ4 push verification workflow"),
    ):
        require(workflow, "tools/tests/macos_gamelibs_alignment.py", context)
        if workflow.count("tools/tests/macos_gamelibs_alignment.py") < 2:
            raise AssertionError(f"{context} should compile and run macos_gamelibs_alignment.py")

    require(macos_debug, "python tools/tests/macos_gamelibs_alignment.py", "macOS debug static guards")
    require(companion_workflow, "tools/tests/game_class_allocator_alignment.py", "in-tree game ABI workflow wiring")


def main() -> None:
    validate_engine_interface_header_parity()
    validate_companion_ci_revision()
    validate_companion_boundary()
    validate_companion_macos_contract()
    validate_game_module_symbol_discipline()
    validate_build_string_contract()
    validate_metadata_contract()
    validate_phase8_docs()
    validate_wiring()
    print("macos_gamelibs_alignment: ok")


if __name__ == "__main__":
    main()
