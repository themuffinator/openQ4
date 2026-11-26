#!/usr/bin/env python3
"""Regression checks for the macOS package UX and release policy contract."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def validate_package_layout_policy_doc() -> None:
    policy = read("docs/dev/macos-package-layout-and-release-policy.md")

    for token in (
        "# macOS Package Layout And Release Policy",
        "self-contained, drag-installable",
        "`openQ4.app`",
        "`Contents/Resources/baseoq4/`",
        "`Contents/Frameworks/`",
        "Mach-O game modules deliberately live in `Contents/Frameworks`",
        "the large `baseoq4` payload is not duplicated",
        "Double-click `openQ4.app` from the mounted signed/notarized DMG payload.",
        "Drag only `openQ4.app` to `/Applications`",
        "Launch the app executable from Terminal with any working directory.",
        "legacy adjacent-package fallback",
        "`fs_basepath`, `fs_cdpath`, and `fs_savepath`",
        "Gatekeeper behavior",
        "First-class macOS releases require signed and notarized DMGs",
        "Unsigned `-unsigned.tar.gz` archives are allowed only for experimental, preview,",
        "tools/build/package_nightly.py",
    ):
        require(policy, token, "macOS package layout and release policy doc")


def validate_signoff_evidence_contract() -> None:
    workflow_doc = read("docs/dev/macos-vm-testing-workflow.md")
    guest = read("tools/macos/guest/openq4-macos-sync-build-test.sh")
    validator = read("tools/macos/validate_signoff_archive.py")
    evidence = read("docs/dev/macos-signoff-evidence.md")
    recorder = read("tools/macos/record_signoff_evidence.py")
    release_completion = read("docs/dev/release-completion.md")

    for token in (
        "docs/dev/macos-package-layout-and-release-policy.md",
        "mounted signed/notarized DMG",
        "drag only the app",
        "whole payload",
        "embedded `Contents/Resources` and `Contents/Frameworks` paths",
        "`fs_basepath`, `fs_cdpath`, and",
        "Gatekeeper behavior",
    ):
        require(workflow_doc, token, "macOS workflow package UX documentation")

    for token in (
        "Package layout contract is self-contained app",
        "mounted signed/notarized DMG",
        "Drag only openQ4.app",
        "whole package payload",
        "Contents/Resources",
        "Contents/Frameworks",
        "unrelated working directory",
        "fs_basepath",
        "fs_cdpath",
        "fs_savepath",
        "Gatekeeper assessment",
    ):
        require(guest, token, "macOS guest signoff package UX checklist")
        require(validator, token, "macOS signoff archive package UX validation")

    for token in (
        "Package layout contract",
        "Mounted DMG launch coverage",
        "Copied package launch coverage",
        "App-only move behavior",
        "Path resolution log coverage",
        "Gatekeeper assessment",
        "First-class macOS release artifacts are signed/notarized DMGs",
    ):
        require(evidence, token, "macOS signoff evidence package UX fields")
        require(recorder, token, "macOS signoff evidence recorder package UX output")

    for token in (
        "macOS Package Layout And Release Policy",
        "First-class macOS release jobs used signed/notarized DMGs",
        "`fs_basepath`, `fs_cdpath`, and `fs_savepath`",
        "Gatekeeper assessment passed",
    ):
        require(release_completion, token, "release completion macOS package UX gate")


def validate_release_workflow_gate() -> None:
    workflow = read(".github/workflows/manual-release.yml")
    building = read("BUILDING.md")
    platform_support = read("docs/dev/platform-support.md")

    for token in (
        "macos_support_tier",
        "macOS release support tier",
        "first-class",
        "OPENQ4_MACOS_SUPPORT_TIER",
        "First-class macOS releases require signed/notarized DMGs",
        "missing Apple signing/notary repository secrets",
        "Preview macOS unsigned/unnotarized tar.gz release artifacts enabled as fallback output",
        "macos_support_tier=",
    ):
        require(workflow, token, "manual release macOS first-class gate")

    for source, context in (
        (building, "build documentation"),
        (platform_support, "platform support documentation"),
    ):
        require(source, "macos_support_tier=preview", context)
        require(source, "macos_support_tier=first-class", context)
        require(source, "signed/notarized DMGs", context)
        require(source, "`-unsigned.tar.gz`", context)


def validate_user_docs_and_package_readme() -> None:
    getting_started = read("docs/user/getting-started.md")
    package_readme = read("assets/release/README.html")
    platform_support = read("docs/dev/platform-support.md")

    require(
        getting_started,
        "drag `openQ4.app` to `/Applications`",
        "getting started macOS package layout",
    )
    require(
        getting_started,
        "The app contains openQ4's `baseoq4` data and the signed game module",
        "getting started self-contained app contract",
    )
    require(
        package_readme,
        "drag <code>openQ4.app</code> to <code>/Applications</code>",
        "packaged README macOS package layout",
    )
    require(
        package_readme,
        "The app contains openQ4's runtime data and the signed game module",
        "packaged README self-contained app contract",
    )
    require(
        platform_support,
        "self-contained drag-installable app",
        "platform support self-contained app contract",
    )
    require(
        platform_support,
        "`Contents/Frameworks`",
        "platform support nested-code contract",
    )


def validate_plan_status_and_ci_wiring() -> None:
    plan = read("docs/dev/archive/2026-10-18/plan/2026-06-30-macos-compatibility-support.md")
    local_runner = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")

    for token in (
        "Phase 3: Package UX and release policy",
        "Phase 3 implementation status",
        "`docs/dev/macos-package-layout-and-release-policy.md`",
        "macos_support_tier=first-class",
        "mounted-DMG, independently dragged-app, whole-package loose-tool, embedded resource/module path, path-resolution, and Gatekeeper results",
    ):
        require(plan, token, "macOS compatibility/support plan Phase 3 status")

    for source, context in (
        (local_runner, "local validation runner"),
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
    ):
        require(source, "macos_package_policy.py", context)

    for source, context in (
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
    ):
        require(source, "python tools/tests/macos_package_policy.py", context)


def main() -> None:
    validate_package_layout_policy_doc()
    validate_signoff_evidence_contract()
    validate_release_workflow_gate()
    validate_user_docs_and_package_readme()
    validate_plan_status_and_ci_wiring()
    print("macos_package_policy: ok")


if __name__ == "__main__":
    main()
