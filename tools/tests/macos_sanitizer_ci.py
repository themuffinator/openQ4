#!/usr/bin/env python3
"""Regression checks for the manual macOS ASan/UBSan workflow."""

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


def validate_workflow() -> None:
    workflow = read(".github/workflows/macos-sanitizer.yml")

    for token in (
        "name: macOS Sanitizer",
        "workflow_dispatch:",
        "runs-on: macos-15",
        "macOS ${{ matrix.bridge_label }} ASan+UBSan",
        "bridge: opengl",
        "bridge: metal",
        "OPENQ4_GAMELIBS_REPO:",
        'MACOSX_DEPLOYMENT_TARGET: "11.0"',
        "ASAN_OPTIONS: halt_on_error=1:abort_on_error=1:detect_leaks=0",
        "UBSAN_OPTIONS: halt_on_error=1:print_stacktrace=1",
        'git -C "${OPENQ4_GAMELIBS_REPO}" fetch --depth 1 origin "${OPENQ4_GAMELIBS_SHA}"',
        'git -C "${OPENQ4_GAMELIBS_REPO}" checkout --detach "${OPENQ4_GAMELIBS_SHA}"',
        'test "$(git -C "${OPENQ4_GAMELIBS_REPO}" rev-parse HEAD)" = "${OPENQ4_GAMELIBS_SHA}"',
        "python tools/tests/macos_sanitizer_ci.py",
        "bash tools/build/meson_setup.sh setup --wipe builddir .",
        "-Dplatform_backend=sdl3",
        "-Dmacos_graphics_bridge=${{ matrix.bridge }}",
        "-Dmacos_openal_provider=system",
        "prepare_macos_openal_soft.sh",
        "--stage-install-dir .install",
        "-Duse_pch=false",
        "-Didlib_asserts=true",
        "-Db_sanitize=address,undefined",
        "bash tools/build/meson_setup.sh compile -C builddir",
        "bash tools/build/meson_setup.sh install -C builddir --no-rebuild --skip-subprojects",
        ".install/openQ4-client_arm64",
        ".install/openQ4-ded_arm64",
        ".install/baseoq4/game-sp_arm64.dylib",
        ".install/baseoq4/game-mp_arm64.dylib",
        "lipo -archs",
        "otool -L",
        "libclang_rt.asan_osx_dynamic.dylib",
        "nm -u",
        "sed -n '1,40p'",
        "Run fail-fast assetless sanitizer smoke",
        "--cases renderer-default-safety-selftest",
        '--basepath ""',
        "--skip-official-pak-validation",
        "ERROR: AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:",
        "Publish sanitizer diagnostics",
        "builddir/meson-logs",
        ".tmp/gamelibs_stage-darwin-arm64/openq4_gamelibs_stage_manifest.json",
        ".tmp/macos-sanitizer-${{ matrix.artifact_suffix }}-runtime",
    ):
        require(workflow, token, "macOS sanitizer workflow")

    reject(workflow, "continue-on-error: true", "macOS sanitizer fail-fast behavior")
    reject(workflow, "| head -", "pipefail-safe sanitizer report truncation")
    reject(workflow, "pull_request:", "manual-only macOS sanitizer trigger")
    reject(workflow, "\n  push:", "manual-only macOS sanitizer trigger")


def validate_wiring_and_docs() -> None:
    validator = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")
    building = read("BUILDING.md")
    platform = read("docs/dev/platform-support.md")
    plan = read("docs/dev/plan/2026-06-30-macos-compatibility-support.md")
    release_completion = read("docs/dev/release-completion.md")
    release_notes = read("docs/dev/releases/v0.8.1.md")

    for source, context in (
        (validator, "local validation runner"),
        (commit, "commit validation workflow"),
        (push, "push validation workflow"),
    ):
        require(source, "macos_sanitizer_ci.py", context)

    for token in (
        "Manual macOS Sanitizer Validation",
        "ASan+UBSan",
        "workflow_dispatch",
        "both OpenGL and Metal bridge configurations",
        "-Duse_pch=false",
        "-Didlib_asserts=true",
        "-Db_sanitize=address,undefined",
        "renderer-default-safety-selftest",
        "does not replace real Apple-hardware gameplay signoff",
    ):
        require(building, token, "macOS sanitizer documentation")

    for token in (
        "manual ASan+UBSan workflow",
        "both OpenGL and Metal bridge variants",
        "assetless fail-fast runtime probe",
    ):
        require(platform, token, "platform-support macOS sanitizer policy")

    for token in (
        "## MAC-008: macOS Sanitizer And Instrumentation Coverage",
        "`.github/workflows/macos-sanitizer.yml`",
        "tools/tests/macos_sanitizer_ci.py",
        "Run the manual sanitizer workflow for both bridges",
    ):
        require(plan, token, "macOS compatibility plan sanitizer status")

    require(
        release_completion,
        "macOS platform-sensitive debugging now has a repeatable manual ASan+UBSan workflow",
        "release completion macOS sanitizer entry",
    )
    require(
        release_notes,
        "macOS diagnostics now include a manually dispatched ASan+UBSan workflow",
        "curated release notes macOS sanitizer entry",
    )


def main() -> None:
    validate_workflow()
    validate_wiring_and_docs()
    print("macos_sanitizer_ci: ok")


if __name__ == "__main__":
    main()
