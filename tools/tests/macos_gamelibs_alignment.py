#!/usr/bin/env python3
"""Static checks for Phase 8 macOS GameLibs alignment."""

from __future__ import annotations

import hashlib
import os
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT.parent / "openQ4-game")).resolve()

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
    "src/renderer/Cinematic.h",
    "src/renderer/ImageOpts.h",
    "src/renderer/Material.h",
    "src/renderer/Model.h",
    "src/renderer/ModelManager.h",
    "src/renderer/RenderSystem.h",
    "src/renderer/RenderWorld.h",
    "src/renderer/RendererCaps.h",
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
    drifted: list[str] = []
    for relative_path in ENGINE_INTERFACE_HEADERS:
        engine_path = ROOT / relative_path
        game_path = GAME_LIBS_ROOT / relative_path
        if not engine_path.is_file():
            raise AssertionError(f"openQ4 engine-interface header not found: {engine_path}")
        if not game_path.is_file():
            raise AssertionError(f"openQ4-game engine-interface header copy not found: {game_path}")
        engine_hash = normalized_content_hash(engine_path)
        game_hash = normalized_content_hash(game_path)
        if engine_hash != game_hash:
            drifted.append(f"{relative_path} (openQ4 {engine_hash[:12]} != openQ4-game {game_hash[:12]})")
    if drifted:
        raise AssertionError(
            "engine-interface header copies in openQ4-game drifted from the authoritative openQ4 versions; "
            "re-sync them from openQ4 src/ (standalone game modules built against drifted headers can "
            "crash at the engine/game ABI boundary despite passing the GAME_API_VERSION check):\n  "
            + "\n  ".join(drifted)
        )


def validate_companion_ci_revision() -> None:
    """Every default CI source pair must contain the tested interface copies."""
    workflow_names = (
        "commit-validation.yml", "push-verification.yml", "linux-arm64-cross.yml",
        "macos-debug.yml", "macos-sanitizer.yml",
    )
    pinned_revision = ""
    for name in workflow_names:
        source = read(f".github/workflows/{name}")
        pins = re.findall(r"^\s*OPENQ4_GAMELIBS_SHA: ([0-9a-f]{40})\s*$", source, re.MULTILINE)
        if len(pins) != 1:
            raise AssertionError(f"{name} must pin exactly one immutable companion revision")
        if pinned_revision and pins[0] != pinned_revision:
            raise AssertionError(f"{name} has a different default companion revision")
        pinned_revision = pins[0]
        reject(source, "git clone --depth 1 https://github.com/themuffinator/openQ4-game.git", name)
        require(source, 'origin "${OPENQ4_GAMELIBS_SHA}"', f"{name} immutable companion fetch")
        require(source, 'checkout --detach "${OPENQ4_GAMELIBS_SHA}"', f"{name} detached companion checkout")
        require(source, 'rev-parse HEAD)', f"{name} companion checkout verification")

    for name in ("manual-release.yml", "macos-universal2-candidate.yml"):
        source = read(f".github/workflows/{name}")
        match = re.search(r"      openq4_game_ref:\n(.*?)        type: string", source, re.DOTALL)
        if match is None or f"default: {pinned_revision}" not in match[1]:
            raise AssertionError(f"{name} must default to the same tested companion revision as CI")

    # A syntactically correct but stale pin would pass the working-tree parity
    # check. Compare the immutable revision CI actually fetches as well.
    for relative_path in ENGINE_INTERFACE_HEADERS:
        completed = subprocess.run(
            ["git", "-C", str(GAME_LIBS_ROOT), "show", f"{pinned_revision}:{relative_path}"],
            check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        if completed.returncode != 0:
            raise AssertionError(f"cannot inspect CI companion header {relative_path}: {completed.stderr.decode(errors='replace')}")
        if completed.stdout.replace(b"\r\n", b"\n") != (ROOT / relative_path).read_bytes().replace(b"\r\n", b"\n"):
            raise AssertionError(f"CI companion pin {pinned_revision} carries a stale engine-interface header: {relative_path}")


def validate_companion_boundary() -> None:
    if (ROOT / "src" / "game").exists():
        raise AssertionError("openQ4 must not grow a local src/game mirror; use openQ4-game as the source input")

    meson = read("meson.build")
    stage_script = read("tools/build/stage_gamelibs.py")
    for token in (
        'root / ".." / "openQ4-game"',
        "OPENQ4_GAMELIBS_REPO",
        "stage_gamelibs.py",
        "gamelibs_stage",
        "openq4_gamelibs_stage_manifest.json",
    ):
        require(meson, token, "openQ4 Meson GameLibs staging contract")

    for token in (
        "copy_game_sources",
        "gameLibsGitCommit",
        "gameLibsGitDirty",
        "projectGitCommit",
        "projectGitDirty",
    ):
        require(stage_script, token, "openQ4 GameLibs staging manifest contract")


def validate_companion_macos_contract() -> None:
    meson = read_game_libs("src/meson.build")
    workflow = read_game_libs(".github/workflows/commit-validation.yml")
    readme = read_game_libs("README.md")

    for token in (
        "is_darwin and cpp.get_id() != 'clang'",
        "['x86_64', 'aarch64'].contains(host_cpu_family)",
        "game_arch = 'arm64'",
        "sp_module_name = 'game-sp_' + game_arch",
        "mp_module_name = 'game-mp_' + game_arch",
        "name_suffix : 'dylib'",
        "'-Wl,-install_name,@loader_path/' + sp_module_name + '.dylib'",
        "'-Wl,-install_name,@loader_path/' + mp_module_name + '.dylib'",
    ):
        require(meson, token, "openQ4-game Meson macOS module contract")

    for token in (
        "runner: macos-15",
        "runner: macos-15-intel",
        "module_arch: arm64",
        "module_arch: x64",
        "game-sp_${module_arch}.dylib",
        "game-mp_${module_arch}.dylib",
        "lipo -archs",
        "otool -D",
        'expected="@loader_path/${module}"',
    ):
        require(workflow, token, "openQ4-game macOS CI dylib/install-name contract")

    for token in (
        "game-sp_arm64.dylib",
        "game-mp_arm64.dylib",
        "@loader_path",
        "ARM64 ABI static checks",
    ):
        require(readme, token, "openQ4-game README macOS GameLibs contract")


def validate_game_module_symbol_discipline() -> None:
    """Issue #90: darwin was the only host that let a game module export its
    whole symbol table, and both modules shared one single-player-flavoured
    idlib archive even though GAME_MPAPI changes which Game_local.h every idlib
    translation unit compiles against."""

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
        "game_idlib_library_mp = static_library(",
        "'openq4_game_idlib_mp',",
        "game_common_cpp_args + ['-DGAME_MPAPI'],",
        "game_idlib_mp_include_dirs",
        "darwin_game_module_export_list = files('tools/build/darwin_game_module.exp')",
        "'-Wl,-exported_symbols_list,' + darwin_game_module_export_list[0].full_path()",
    ):
        require(engine_meson, token, "engine per-flavour game idlib and darwin export list")

    if module_meson.count("link_with: game_idlib_library_mp,") != 3:
        raise AssertionError("every multiplayer game module target must link the multiplayer idlib archive")
    if module_meson.count("link_with: game_idlib_library,") != 3:
        raise AssertionError("every single-player game module target must link the single-player idlib archive")
    if module_meson.count("gnu_symbol_visibility: 'hidden',") != 4:
        raise AssertionError("darwin and linux game modules must both hide non-exported symbols")

    companion_meson = read_game_libs("src/meson.build")
    for token in (
        "idlib_mp = static_library(",
        "'idLibMP',",
        "game_mpapi_define_arg",
        "darwin_game_module_export_list",
        "'-Wl,-exported_symbols_list,'",
        "gnu_symbol_visibility : 'hidden',",
    ):
        require(companion_meson, token, "openQ4-game per-flavour idlib and darwin export list")

    # _DEBUG changes shared struct layouts (srfTriangles_t::description,
    # Heap.h's MemScopedTag). The engine never defines it on Clang, so the
    # companion build must not either.
    reject(companion_meson, "common_defines += ['_DEBUG']", "companion _DEBUG parity with the engine")

    # The SDK SIMD implementations use 32-bit MSVC inline assembly. Keep them
    # available to the x86 build while excluding the complete legacy family
    # everywhere Simd.cpp intentionally selects the generic processor.
    legacy_x86_simd_exclusion = """if host_cpu_family != 'x86'
  idlib_excludes += [
    'idlib/math/Simd_3DNow.cpp',
    'idlib/math/Simd_MMX.cpp',
    'idlib/math/Simd_SSE.cpp',
    'idlib/math/Simd_SSE2.cpp',
    'idlib/math/Simd_SSE3.cpp',
  ]
endif"""
    require(companion_meson, legacy_x86_simd_exclusion, "companion architecture-specific SIMD source selection")

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
        "game-sp_*.dylib",
        "game-mp_*.dylib",
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
    plan = read("docs/dev/plans/2026-06-30-apple-support-no-macos-access.md")
    compatibility_plan = read("docs/dev/plan/2026-06-30-macos-compatibility-support.md")
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

    require(signoff_evidence, "openQ4 and `openQ4-game` commit fields", "macOS signoff evidence index")

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
    require(companion_workflow, "tools/tests/arm64_abi_contract.py", "openQ4-game static ABI workflow wiring")


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
