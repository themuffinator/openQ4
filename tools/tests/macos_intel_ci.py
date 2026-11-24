#!/usr/bin/env python3
"""Regression checks for the evidence-honest experimental macOS Intel CI corridor."""

from __future__ import annotations

import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)).resolve()


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def read_game(relative_path: str) -> str:
    path = GAME_LIBS_ROOT / relative_path
    if not path.is_file():
        raise AssertionError(f"openQ4-game file not found: {path}")
    return path.read_text(encoding="utf-8")


def require(source: str, needle: str, context: str) -> None:
    if needle not in source:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(source: str, needle: str, context: str) -> None:
    if needle in source:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def validate_engine_corridor() -> None:
    meson = read("meson.build")
    validator = read("tools/validation/openq4_validate.py")
    dedicated = read("tools/tests/macos_dedicated_server_smoke.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")

    for token in (
        "if host_cpu_family == 'x86_64'",
        "binary_arch = 'x64'",
        "engine_client_binary_name = meson.project_name() + '-client_' + binary_arch",
        "engine_ded_binary_name = meson.project_name() + '-ded_' + binary_arch",
    ):
        require(meson, token, "openQ4 macOS x64 Meson naming")

    for token in (
        '"x64": frozenset(("x86_64",))',
        "validate_macos_binary_architectures(",
        "validate_macos_staged_metadata(",
    ):
        require(validator, token, "openQ4 staged x64 Mach-O validation")

    for token in (
        'if machine in {"x86_64", "amd64"}',
        'return "x64"',
        'f"openQ4-ded_{arch}"',
        'f"game_{arch}.dylib"',
    ):
        require(dedicated, token, "macOS dedicated smoke Intel support")

    # Intel is not a release target: pull requests skip it, and pushes to main
    # build one bridge variant. The manual universal2 candidate builds both.
    for token in ("macos-x64:", "macos-15-intel", "Intel x64"):
        reject(commit, token, "commit validation Intel corridor")

    intel_start = push.index("          - name: macOS Intel x64 OpenGL Push Verification")
    intel_entry = push[intel_start : push.index("\n    steps:", intel_start)]
    for token in (
        "os: macos-15-intel",
        "artifact_name: macos-x64-opengl",
        "runtime_smoke: true",
        "native_locales: false",
        "macos_graphics_bridge: opengl",
    ):
        require(intel_entry, token, "push verification Intel matrix entry")
    if push.count("os: macos-15-intel") != 1:
        raise AssertionError("push verification must build exactly one Intel bridge variant")
    for token in (
        'runtime_cases="renderer-foundation-selftests,renderer-pbr-visible-selftest,renderer-cluster-grid-selftest,renderer-default-safety-selftest"',
        "python tools/tests/macos_dedicated_server_smoke.py",
        "push-${{ matrix.artifact_name }}-dedicated-smoke",
    ):
        require(push, token, "push verification Intel corridor")
    reject(push, "macOS Intel x64 Metal Push Verification", "push verification Intel corridor")

    candidate = read(".github/workflows/macos-universal2-candidate.yml")
    for token in ('"runner": "macos-15-intel"', 'expected_arch="x86_64"', 'actual_arch="$(uname -m)"'):
        require(candidate, token, "universal2 candidate Intel thin builds")


def validate_companion_corridor() -> None:
    meson=read('meson.build')
    modules=read('content/baseoq4/meson.build')
    workflow=read('.github/workflows/commit-validation.yml')
    for token in ("binary_arch = 'x64'", "binary_arch = 'arm64'"):
        require(meson,token,'canonical macOS game architecture')
    require(modules,"'-Wl,-install_name,@loader_path/' + game_binary_name + '.dylib'",'canonical macOS game install name')
    push=read('.github/workflows/push-verification.yml')
    for token in ('macos-15',"MACOSX_DEPLOYMENT_TARGET: \"11.0\""):
        require(workflow,token,'canonical macOS CI corridor')
    require(push,'macos-15-intel','canonical macOS Intel CI corridor')
    for source in (workflow,push):
        reject(source,'openQ4-game.git','retired companion checkout')
    require(read('tools/validation/openq4_validate.py'),'lipo','staged Mach-O architecture guard')


def validate_policy_and_wiring() -> None:
    manual = read(".github/workflows/manual-release.yml")
    policy = read("docs/dev/macos-support-matrix-policy.md")
    building = read("BUILDING.md")
    platform = read("docs/dev/platform-support.md")
    plan = read("docs/dev/archive/2026-10-18/plan/2026-06-30-macos-compatibility-support.md")
    release_completion = read("docs/dev/release-completion.md")
    release_notes = read("docs/dev/releases/v0.8.1.md")
    validator = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")

    for token in ("macos-x64", "macos-universal2", "macOS x64", "macOS universal2"):
        reject(manual, token, "manual release must remain arm64-only")

    for token in (
        "### Experimental Intel CI Corridor",
        "macos-15-intel",
        "hosted-runner reference",
        "experimental build/loader evidence only",
        "does not publish",
        "real Intel Apple hardware",
    ):
        require(policy, token, "macOS Intel matrix policy")

    require(building, "Hosted `macos-15-intel` jobs", "build documentation Intel corridor")
    require(platform, "Dedicated `macos-15-intel` engine and game-module jobs", "platform support Intel corridor")

    for token in (
        "[x] Add a standard hosted Intel Mac runner",
        "[x] Add `../openQ4-game` macOS x86_64 CI",
        "[x] Add openQ4 macOS x86_64 staged-package validation",
        "[ ] Record clean hosted results",
        "[ ] Intel release expansion remains deferred",
    ):
        require(plan, token, "macOS compatibility Intel status")

    require(release_completion, "Experimental Intel macOS CI is now configured", "release completion Intel entry")
    require(release_notes, "experimental Intel x64 macOS CI", "curated release notes Intel entry")

    for source, context in (
        (validator, "local validation runner"),
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
    ):
        require(source, "macos_intel_ci.py", context)


def main() -> None:
    validate_engine_corridor()
    validate_companion_corridor()
    validate_policy_and_wiring()
    print("macos_intel_ci: ok")


if __name__ == "__main__":
    main()
