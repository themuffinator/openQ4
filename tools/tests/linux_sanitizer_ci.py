#!/usr/bin/env python3
"""Regression checks for Linux sanitizer validation coverage."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def validate_commit_workflow() -> None:
    workflow = read(".github/workflows/commit-validation.yml")
    start = workflow.index("  linux-sanitizer:")
    end = workflow.index("\n  linux-wayland:", start)
    source = workflow[start:end]

    require(source, "linux-sanitizer:", "commit validation sanitizer job")
    require(source, "Linux x64 ASan+UBSan Commit Validation", "commit validation sanitizer job")
    require(source, "runs-on: ubuntu-24.04", "commit validation sanitizer runner")
    require(source, "ASAN_OPTIONS: halt_on_error=1:detect_leaks=0", "commit validation ASan options")
    require(source, "UBSAN_OPTIONS: halt_on_error=1:print_stacktrace=1", "commit validation UBSan options")
    require(source, "bash tools/validation/validate_pr.sh", "commit validation sanitizer runner")
    require(source, "--skip-python-tests", "commit validation sanitizer focus")
    require(source, "--no-install", "commit validation sanitizer build/install separation")
    require(source, "--build-dir .tmp/validation/linux-sanitizer-builddir", "commit validation sanitizer builddir")
    require(source, "--buildtype debugoptimized", "commit validation sanitizer buildtype")
    require(source, "--jobs 2", "commit validation sanitizer parallelism")
    require(source, "--extra-setup-arg=-Duse_pch=false", "commit validation sanitizer PCH opt-out")
    require(source, "--extra-setup-arg=-Db_sanitize=address,undefined", "commit validation ASan+UBSan setup")
    require(source, "Stage sanitizer runtime", "commit validation sanitizer staging")
    require(source, "bash tools/build/meson_setup.sh install", "commit validation sanitizer staging wrapper")
    require(source, "--no-rebuild", "commit validation sanitizer staging")
    require(source, "--skip-subprojects", "commit validation sanitizer staging")
    require(source, "Run sanitizer native Wayland smoke", "commit validation sanitizer runtime")
    require(source, "weston --backend=headless-backend.so", "commit validation sanitizer compositor")
    require(source, "--socket=openq4-sanitizer-wayland", "commit validation sanitizer compositor socket")
    require(source, "unset DISPLAY", "commit validation native Wayland isolation")
    require(source, "SDL_VIDEO_DRIVER=wayland", "commit validation native Wayland driver")
    require(source, "SDL_VIDEODRIVER=wayland", "commit validation native Wayland legacy driver")
    require(source, "LIBGL_ALWAYS_SOFTWARE=1", "commit validation software renderer")
    require(source, "OPENQ4_WAYLAND_DISABLE_LIBDECOR=1", "commit validation deterministic Wayland decorations")
    require(source, "--cases sdl3-wayland-window-lifecycle", "commit validation sanitizer lifecycle case")
    require(source, "--timeout 120", "commit validation bounded sanitizer lifecycle case")
    require(source, '--basepath ""', "commit validation assetless base path")
    require(source, "--skip-official-pak-validation", "commit validation assetless package policy")
    require(source, '--output-dir "${output_dir}"', "commit validation sanitizer report output")
    require(source, "commit-linux-sanitizer-build-logs", "commit validation sanitizer artifact")
    require(source, ".tmp/validation/linux-sanitizer-builddir/meson-logs", "commit validation sanitizer logs")
    require(source, ".tmp/gamelibs_stage-linux-x64/openq4_gamelibs_stage_manifest.json", "commit validation sanitizer staging manifest")
    require(source, ".tmp/renderer-validation/sanitizer", "commit validation sanitizer runtime artifacts")
    require(workflow, "python tools/tests/linux_sanitizer_ci.py", "commit script-smoke sanitizer guard")

    for dependency in (
        "libdecor-0-dev",
        "libdrm-dev",
        "libegl1-mesa-dev",
        "libgl1-mesa-dev",
        "libopenal-dev",
        "libwayland-dev",
        "libx11-dev",
        "libxxf86vm-dev",
        "weston",
    ):
        require(source, dependency, "commit validation sanitizer dependencies")


def validate_push_workflow() -> None:
    source = read(".github/workflows/push-verification.yml")

    require(source, "python tools/tests/linux_sanitizer_ci.py", "push script-smoke sanitizer guard")
    require(source, "tools/tests/linux_sanitizer_ci.py", "push script-smoke sanitizer py_compile")


def validate_validation_runner() -> None:
    source = read("tools/validation/openq4_validate.py")

    require(source, "linux_sanitizer_ci.py", "validation runner sanitizer guard")
    require(source, "--extra-setup-arg", "validation runner sanitizer Meson passthrough")
    require(source, "args.extra_setup_arg", "validation runner sanitizer Meson passthrough")
    require(source, "--no-install", "validation runner compile-only support")

    renderer = read("tools/tests/renderer_validation_matrix.py")
    require(renderer, '"id": "sdl3-wayland-window-lifecycle"', "sanitizer Wayland lifecycle case")
    require(renderer, '"assetless": True', "sanitizer assetless runtime case")
    require(renderer, 'case_basepath = "" if case_assetless else basepath', "assetless base-path enforcement")
    require(
        renderer,
        "case_skip_official_pak_validation = skip_official_pak_validation or case_assetless",
        "assetless stock-package bypass",
    )


def validate_documentation() -> None:
    source = read("BUILDING.md")

    require(source, "Linux Sanitizer Validation", "BUILDING sanitizer section")
    require(source, "ASan+UBSan", "BUILDING sanitizer section")
    require(source, "-Db_sanitize=address,undefined", "BUILDING sanitizer command")
    require(source, "-Duse_pch=false", "BUILDING sanitizer command")
    require(source, ".tmp/validation/linux-sanitizer-builddir", "BUILDING sanitizer builddir")
    require(source, "sdl3-wayland-window-lifecycle", "BUILDING sanitizer native Wayland runtime")
    require(source, "No stock Quake 4 assets are required", "BUILDING sanitizer assetless guarantee")


def validate_release_note() -> None:
    source = read("docs/dev/release-completion.md")

    require(source, "Linux sanitizer coverage now catches native Wayland runtime regressions", "release completion notes")
    require(source, "ASan+UBSan", "release completion notes")
    require(source, "openQ4-game staging manifest", "release completion notes")


def validate_bse_without_pch() -> None:
    meson = read("meson.build")
    require(meson, "common_header_cpp_args = ['-include', common_header_path]", "non-PCH common-header fallback")
    require(meson, "engine_cpp_args = shared_cpp_args + common_header_cpp_args", "engine non-PCH common header")
    require(meson, "game_common_cpp_args = shared_cpp_args + common_header_cpp_args", "game non-PCH common header")

    bse_sources = sorted((ROOT / "src" / "bse").glob("*.cpp"))
    if not bse_sources:
        raise AssertionError("No in-tree BSE sources found")
    for path in bse_sources:
        source = path.read_text(encoding="utf-8")
        require(source, '#include "../idlib/precompiled.h"', f"{path.name} explicit engine declarations")
        require(source, "#pragma hdrstop", f"{path.name} precompiled-header compatibility")


def validate_cross_dso_sanitizer_boundary() -> None:
    meson = read("meson.build")
    require(
        meson,
        "linux_cross_dso_sanitizer_cpp_args += ['-fno-sanitize=vptr']",
        "Linux engine/game cross-DSO sanitizer boundary",
    )
    require(
        meson,
        "engine_cpp_args = shared_cpp_args + common_header_cpp_args + ['-D__DOOM_DLL__'] + linux_cross_dso_sanitizer_cpp_args",
        "Linux engine sanitizer scope",
    )
    require(
        meson,
        "game_common_cpp_args = shared_cpp_args + common_header_cpp_args + ['-DGAME_DLL'] + linux_cross_dso_sanitizer_cpp_args",
        "Linux game sanitizer scope",
    )
    require(
        meson,
        "bse_cpp_args = shared_cpp_args + common_header_cpp_args + linux_cross_dso_sanitizer_cpp_args",
        "BSE cross-DSO sanitizer scope",
    )
    require(
        meson,
        "renderer_gl_module_args = engine_cpp_args + [",
        "OpenGL renderer cross-DSO sanitizer scope",
    )
    require(
        meson,
        "renderer_vk_module_args = engine_cpp_args + [",
        "Vulkan renderer cross-DSO sanitizer scope",
    )
    reject(meson, "shared_cpp_args += ['-fno-sanitize=vptr']", "over-broad UBSan vptr exclusion")
    reject(meson, "-fno-sanitize=pointer-overflow", "masked renderer file-ownership defect")
    require(meson, "dedicated_engine_cpp_args = engine_cpp_args", "dedicated sanitizer inheritance")
    require(
        meson,
        "Keep AddressSanitizer and every other requested UBSan check enabled.",
        "Linux game-module sanitizer scope",
    )


def main() -> None:
    validate_commit_workflow()
    validate_push_workflow()
    validate_validation_runner()
    validate_documentation()
    validate_release_note()
    validate_bse_without_pch()
    validate_cross_dso_sanitizer_boundary()
    print("linux_sanitizer_ci: ok")


if __name__ == "__main__":
    main()
