#!/usr/bin/env python3
"""Static contracts for the non-publishing macOS universal2 candidate lane."""

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


def require_before(source: str, first: str, second: str, context: str) -> None:
    require(source, first, context)
    require(source, second, context)
    if source.index(first) >= source.index(second):
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def validate_candidate_workflow() -> None:
    workflow = read(".github/workflows/macos-universal2-candidate.yml")

    for token in (
        "name: macOS Universal2 Release Candidate",
        "workflow_dispatch:",
        "graphics_bridge:",
        "macos_signing_mode:",
        "default: ad-hoc",
        "contents: read",
        "source_sha:",
        "game_sha:",
        "thin_matrix:",
        "bridge_matrix:",
        "git status --porcelain --untracked-files=all",
        "openq4_version.py",
        "macos-15-intel",
        "macos-15",
        "Verify requested native architecture",
        "expected_arch=\"x86_64\"",
        "--expected-project-commit",
        "--buildtype=debugoptimized",
        "assemble_macos_universal2.py record",
        "Archive thin universal2 candidate payload with modes",
        "tar -C .install -czf",
        "Restore mode-preserving thin payloads",
        "tar -xzf",
        "assemble_macos_universal2.py assemble",
        "--arch universal2",
        "package_release.py",
        "--macos-signing-mode ad-hoc",
        "--macos-signing-mode developer-id",
        "--macos-notarize",
        "lipo -archs",
        "Expected exact arm64 x86_64 slices",
        "otool -arch",
        "codesign --verify",
        "xcrun stapler validate",
        "spctl --assess",
        "Finder-style directory",
        "game_universal2.dylib",
        "retention-days: 90",
    ):
        require(workflow, token, "macOS universal2 candidate workflow")

    reject(workflow, "contents: write", "macOS universal2 candidate workflow permissions")
    reject(workflow, "Fetch pinned openQ4-game source", "single-checkout game source ownership")
    reject(workflow, "      openq4_game_ref:", "single-checkout source selection")
    require(workflow, '--source-manifest builddir/openq4_game_sources.json', 'in-tree universal2 provenance')
    reject(workflow, "gh release", "macOS universal2 candidate workflow publication")
    reject(workflow, "softprops/action-gh-release", "macOS universal2 candidate workflow publication")
    reject(workflow, "path: .install", "macOS universal2 candidate thin artifact transfer")

    thin_start = workflow.index("\n  thin_build:")
    assemble_start = workflow.index("\n  assemble:", thin_start)
    thin_job = workflow[thin_start:assemble_start]
    install_command = "bash tools/build/meson_setup.sh install -C builddir --no-rebuild --skip-subprojects"
    normalize_step_name = "- name: Normalize thin universal2 payload"
    smoke_step = "name: Run thin dedicated-server smoke"
    stage_step_name = "- name: Stage pinned MoltenVK for thin payload"
    record_step = "name: Record thin universal2 provenance"
    preparation_context = "macOS universal2 candidate thin payload preparation"
    if thin_job.count(stage_step_name) != 1:
        raise AssertionError(f"Expected exactly one {stage_step_name!r} in {preparation_context}")

    if thin_job.count(normalize_step_name) != 1:
        raise AssertionError(f"Expected exactly one {normalize_step_name!r} in {preparation_context}")
    normalize_start = thin_job.index(normalize_step_name)
    normalize_end = thin_job.index("\n      - name:", normalize_start + len(normalize_step_name))
    normalize_step = thin_job[normalize_start:normalize_end]

    stage_start = thin_job.index(stage_step_name)
    stage_end = thin_job.index("\n      - name:", stage_start + len(stage_step_name))
    stage_step = thin_job[stage_start:stage_end]
    normalize_command = "python tools/build/assemble_macos_universal2.py prepare"
    prepare_command = "bash tools/build/prepare_macos_moltenvk.sh --output-dir .install"
    verify_command = "bash tools/build/prepare_macos_moltenvk.sh --verify-only --output-dir .install"

    require_before(thin_job, install_command, normalize_step_name, preparation_context)
    require(normalize_step, normalize_command, preparation_context)
    require(normalize_step, "--install-root .install", preparation_context)
    require(normalize_step, '--arch "${{ matrix.binary_arch }}"', preparation_context)
    reject(normalize_step, "\n        if:", preparation_context)
    require_before(thin_job, normalize_step_name, smoke_step, preparation_context)
    require_before(thin_job, smoke_step, stage_step_name, preparation_context)
    require_before(stage_step, prepare_command, verify_command, preparation_context)
    require_before(thin_job, stage_step_name, record_step, preparation_context)
    reject(stage_step, "\n        if:", preparation_context)

    package_step_name = "- name: Package universal2 release candidate"
    candidate_dependency_step = "- name: Install release documentation dependency"
    candidate_setup_step = "- name: Setup Python"
    package_start = workflow.index(package_step_name, assemble_start)
    package_end = workflow.index("\n      - name:", package_start + len(package_step_name))
    package_step = workflow[package_start:package_end]
    package_suffix_argument = '"--package-suffix=${package_suffix}"'
    if package_step.count(package_suffix_argument) != 2:
        raise AssertionError(
            "Expected common and Developer-ID candidate package arrays to pass "
            "the leading-hyphen package suffix as one argv token"
        )
    reject(package_step, '--package-suffix "${package_suffix}"', "universal2 candidate package argument safety")
    candidate_assemble_job = workflow[assemble_start:]
    if candidate_assemble_job.count(candidate_dependency_step) != 1:
        raise AssertionError("Expected exactly one release documentation dependency step in the candidate assembler")
    require_before(candidate_assemble_job, candidate_setup_step, candidate_dependency_step, "candidate packaging dependency")
    require_before(candidate_assemble_job, candidate_dependency_step, package_step_name, "candidate packaging dependency")
    candidate_dependency_start = workflow.index(candidate_dependency_step, assemble_start)
    candidate_dependency_end = workflow.index(
        "\n      - name:", candidate_dependency_start + len(candidate_dependency_step)
    )
    candidate_dependency = workflow[candidate_dependency_start:candidate_dependency_end]
    require(candidate_dependency, "python -m pip install markdown", "candidate packaging dependency")
    reject(candidate_dependency, "\n        if:", "candidate packaging dependency")

    finder_smoke_step_name = "- name: Smoke universal2 app runtime from a Finder-style directory"
    finder_smoke_start = workflow.index(finder_smoke_step_name, package_end)
    finder_smoke_end = workflow.index("\n      - name:", finder_smoke_start + len(finder_smoke_step_name))
    finder_smoke = workflow[finder_smoke_start:finder_smoke_end]
    reject(finder_smoke, "\n        if:", "candidate Finder-style smoke execution")
    canonical_package_dir = 'package_dir="${GITHUB_WORKSPACE}/${{ steps.package.outputs.package_dir }}"'
    require(finder_smoke, canonical_package_dir, "candidate Finder-style package path")
    require_before(finder_smoke, canonical_package_dir, 'cd "${smoke_cwd}"', "candidate Finder-style package path")
    reject(
        finder_smoke,
        'package_dir="${{ steps.package.outputs.package_dir }}"',
        "candidate Finder-style relative package path",
    )
    for token in (
        'smoke_exit_status="${smoke_root}/app-exit-status"',
        'smoke_console="${smoke_root}/console.log"',
        '+quit > "${smoke_console}" 2>&1 &',
        "printf '%s\\n' \"${app_status}\" > \"${smoke_exit_status}\"",
        'app_status="$(tr -d \'[:space:]\' < "${smoke_exit_status}")"',
        'if ! [[ "${app_status}" =~ ^[0-9]+$ ]] || [ "${app_status}" -gt 127 ]; then',
        'require_smoke_output "RendererDefaultSafety self-test passed"',
        'require_smoke_output "Filesystem paths:"',
        'require_smoke_output "fs_cdpath=\'${package_dir}/openQ4.app/Contents/Resources\'"',
        'require_smoke_output "Selected game module: logical=\'game_sp\' binary=\'game_universal2\'"',
        'grep -Eiq \'(^|[[:space:]])(FATAL|Sys_Error):\' "${smoke_console}" "${smoke_log}"',
        "Universal2 app smoke output contains a fatal startup diagnostic.",
        'if [ "${app_status}" -ne 0 ]; then',
        'if [ -f "${smoke_timeout_marker}" ]; then',
        "exit 124",
    ):
        require(finder_smoke, token, "candidate Finder-style launcher status policy")
    reject(finder_smoke, 'exit "${app_status}"', "candidate Finder-style launcher status propagation")
    require_before(
        finder_smoke,
        'app_status="$(tr -d \'[:space:]\' < "${smoke_exit_status}")"',
        'require_smoke_output "RendererDefaultSafety self-test passed"',
        "candidate Finder-style marker validation",
    )
    reject(finder_smoke, 'grep -F "Filesystem paths:" "${smoke_log}"', "candidate pre-log marker source")


def validate_docs_and_wiring() -> None:
    design = read("docs/dev/macos-universal2-design.md")
    plan = read("docs/dev/archive/2026-10-18/plan/2026-06-30-macos-compatibility-support.md")
    completion = read("docs/dev/release-completion.md")
    release_notes = read("docs/dev/releases/v0.8.1.md")
    validator = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")

    for source, context in (
        (design, "universal2 design"),
        (plan, "macOS compatibility plan"),
        (completion, "release completion"),
        (release_notes, "curated release notes"),
    ):
        require(source, "non-publishing", context)
        require(source, "arm64-only", context)

    for source, context in (
        (validator, "local validation runner"),
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
    ):
        require(source, "macos_universal2_release_candidate.py", context)

    # Pull requests keep the mode-preserving arm64 thin payload; universal2
    # assembly runs only in the manual candidate lane checked above.
    for token in ("  macos-x64:", "  macos-universal2:"):
        reject(commit, token, "commit validation universal2 scope")
    commit_arm64 = commit[commit.index("  macos-arm64:"):]
    for token in (
        "Archive macOS ARM64 staged payload with modes",
        "tar -C .install -czf",
    ):
        require(commit_arm64, token, "commit-validation thin payload archive")
    reject(commit_arm64, "path: .install", "commit-validation thin payload archive")


def main() -> None:
    validate_candidate_workflow()
    validate_docs_and_wiring()
    print("macos_universal2_release_candidate: ok")


if __name__ == "__main__":
    main()
