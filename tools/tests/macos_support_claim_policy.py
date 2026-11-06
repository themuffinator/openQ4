#!/usr/bin/env python3
"""Regression checks for macOS and ARM64 support-claim wording."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PLAN_PATH = "docs/dev/archive/2026-10-18/plans/2026-06-30-apple-support-no-macos-access.md"
HISTORICAL_ISSUE_73_RELEASES = {
    "v0.6.5.md",
    "v0.8.1.md",
    "v0.9.0.md",
    "v0.10.000.md",
}
# Notes published while macOS was experimental, before the 2026-09-18 move to
# preview. They keep the wording they shipped with; later notes say preview.
HISTORICAL_ISSUE_98_RELEASES = {
    "v0.11.0.md",
    "v0.12.0.md",
    "v0.13.0.md",
    "v0.13.1.md",
}
HISTORICAL_EXPERIMENTAL_MACOS_RELEASES = (
    HISTORICAL_ISSUE_73_RELEASES | HISTORICAL_ISSUE_98_RELEASES | {"v0.13.2.md"}
)


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def require_any(haystack: str, needles: tuple[str, ...], context: str) -> None:
    if not any(needle in haystack for needle in needles):
        formatted = ", ".join(repr(needle) for needle in needles)
        raise AssertionError(f"Missing one of {formatted} in {context}")


def release_files() -> list[Path]:
    return sorted((ROOT / "docs" / "dev" / "releases").glob("v*.md"))


def validate_release_facing_docs() -> None:
    expected_tokens = {
        "README.md": (
            "preview Apple Silicon/arm64 macOS",
            "OpenGL/Metal bridge packages",
            "players have run them only on current macOS",
        ),
        "BUILDING.md": (
            "Preview manual macOS release artifacts are Apple Silicon/arm64 only",
            "Intel Mac and universal2 packages are not published",
            "Rosetta is not a supported release target",
            "Metal bridge",
            "comparison-only",
        ),
        "docs/user/getting-started.md": (
            "Our GitHub macOS packages remain a preview",
            "Apple Silicon/arm64 Macs on macOS 11 or later",
            "nothing older than the current macOS has been tested",
            "Our GitHub releases do not yet publish Intel Mac or universal2 packages",
            "Rosetta is not a supported release target",
            "Metal bridge",
            "not a native Metal renderer",
        ),
        "docs/dev/platform-support.md": (
            "## Support Tiers",
            "Current architecture policy: `arm64 only` for preview Apple Silicon/arm64 release packages.",
            "Unsupported current macOS release targets: Intel Mac/`x86_64`, universal2 packages, and Rosetta",
            "Current package variants: `OpenGL` and `Metal bridge`",
            "not a native Metal renderer",
            "comparison-only diagnostics",
            "docs/dev/macos-signoff-evidence.md",
        ),
        "docs/dev/macos-support-matrix-policy.md": (
            "## Support Tier",
            "What the preview does not cover:",
            "The preview returns to experimental",
        ),
        "docs/dev/macos-signoff-evidence.md": (
            "## Community Hardware Reports",
            "None of them is an accepted signoff archive",
        ),
        "assets/release/README.html": (
            "preview Apple Silicon/arm64 macOS",
            "macOS support is a preview",
            "Apple Silicon/arm64 Macs on macOS 11 or later",
            "nothing older than the current macOS has been tested",
            "Intel Mac and universal2 packages are not published yet",
            "Rosetta is not a supported release target",
            "Metal bridge",
            "not a native Metal renderer",
        ),
    }

    for relative_path, tokens in expected_tokens.items():
        source = read(relative_path)
        for token in tokens:
            require(source, token, relative_path)


def validate_arm64_tier_labels() -> None:
    # Windows ARM64 assets carry no tier suffix, so these sentences and the
    # release-body section are the only label they get.
    for relative_path, token in (
        ("README.md", "experimental Windows ARM64"),
        ("docs/user/getting-started.md", "Windows ARM64 packages are experimental"),
        ("assets/release/README.html", "Windows ARM64 packages are experimental"),
        ("docs/dev/platform-support.md", "- Windows ARM64 is experimental."),
        ("docs/dev/engine-capability-matrix.md", "| Windows ARM64 client/server | **Experimental** |"),
        (".github/ISSUE_TEMPLATE/windows-arm64-report.yml", "Windows ARM64 packages are experimental"),
        (".github/scripts/announce-release-discord.mjs", '"Windows ARM64 Installer (experimental)"'),
        ("docs/dev/platform-support.md", "| Windows ARM64 | Experimental |"),
        # Linux ARM64 stays a preview behind its own evidence gate.
        ("docs/dev/platform-support.md", "| Linux ARM64 (`aarch64`) | Preview |"),
    ):
        require(read(relative_path), token, relative_path)

    # Push and pull-request CI start the Windows ARM64 dedicated server, but
    # never the client, and no hardware report exists. The docs say both, stop
    # saying that nothing starts it, and the lanes they cite must still exist.
    for relative_path in (
        "docs/dev/platform-support.md",
        "docs/dev/engine-capability-matrix.md",
        ".github/ISSUE_TEMPLATE/windows-arm64-report.yml",
    ):
        source = read(relative_path)
        require(source, "starts the dedicated server without game data", relative_path)
        require(source, "client has never been started by automation", relative_path)
    for relative_path in (
        "docs/user/getting-started.md",
        "assets/release/README.html",
        ".github/workflows/manual-release.yml",
    ):
        require(read(relative_path), "nothing has started the game client automatically yet", relative_path)
    for relative_path in (
        "README.md",
        "BUILDING.md",
        "docs/user/getting-started.md",
        "assets/release/README.html",
        "docs/dev/platform-support.md",
        "docs/dev/engine-capability-matrix.md",
        ".github/ISSUE_TEMPLATE/windows-arm64-report.yml",
        ".github/workflows/manual-release.yml",
    ):
        source = read(relative_path)
        for stale in (
            "no automated test starts them yet",
            "no automated test starts it",
            "No workflow starts it",
            "no workflow starts its binaries",
            "no job starts its binaries",
            "binary has been started by automation",
            "built only by the manual release workflow",
        ):
            reject(source, stale, relative_path)
    for relative_path in (".github/workflows/push-verification.yml", ".github/workflows/commit-validation.yml"):
        require(read(relative_path), "python tools/tests/windows_dedicated_server_smoke.py --arch arm64", relative_path)

    workflow = read(".github/workflows/manual-release.yml")
    for token in (
        "## macOS Support",
        "## Windows ARM64 Support",
        "Windows ARM64 packages are experimental.",
        "MACOS_SUPPORT_TIER: ${{ needs.metadata.outputs.macos_support_tier }}",
        "preview_claim_status",
    ):
        require(workflow, token, "manual release platform-support notes")
    # The macOS and Windows sections must follow the first-class Linux ARM64
    # preview-claim scan, or the macOS preview wording would trip it.
    if workflow.index("## macOS Support") < workflow.index("preview_claim_status"):
        raise AssertionError("macOS release notes are appended before the Linux ARM64 preview-claim scan")


def validate_release_completion_guard() -> None:
    release_completion = read("docs/dev/release-completion.md")

    for token in (
        "## macOS Support Claim Guard",
        'Release notes that mention macOS say "preview Apple Silicon/arm64 macOS"',
        "Any first-class, stable, or fully supported macOS claim cites the current release entry",
        "docs/dev/macos-signoff-evidence.md",
        "Intel Mac, universal2, and Rosetta appear only as unsupported, not-published, or future-policy items",
        "`macos_graphics_bridge=metal` is described as a Metal bridge around the OpenGL renderer",
        "`platform_backend=native` on macOS is described as comparison-only diagnostic infrastructure",
        "Curated release notes describe macOS as a preview.",
        "these community reports support the preview but do not satisfy the macOS Evidence Gate",
        "## Windows ARM64 Support Claim Guard",
    ):
        require(release_completion, token, "macOS support claim guard")


def validate_curated_release_notes() -> None:
    releases = release_files()
    if not releases:
        raise AssertionError("No curated release notes found under docs/dev/releases")

    # Saying macOS is "no longer experimental" is simply the preview tier now;
    # these phrases claim more than a preview.
    promotion_phrases = (
        "first-class macos",
        "fully supported macos",
        "macos support is stable",
        "stable macos support",
        "production-ready macos",
        "macos support has graduated",
        "macos support is no longer a preview",
        "macos support is no longer in preview",
    )

    for release_path in releases:
        text = release_path.read_text(encoding="utf-8")
        if "macOS" not in text:
            continue

        context = release_path.relative_to(ROOT).as_posix()
        if release_path.name in HISTORICAL_EXPERIMENTAL_MACOS_RELEASES:
            require(text, "experimental macOS", context)
        else:
            # "preview macOS" word order keeps the first-class Linux ARM64
            # preview-claim scan in manual-release.yml from misreading it.
            require_any(text, ("preview macOS", "preview Apple Silicon"), context)
        require(text, "Apple Silicon/arm64", context)
        require(text, "Intel Mac", context)
        require_any(text, ("not supported", "not published"), context)
        require(text, "Rosetta", context)
        require(text, "Metal bridge", context)
        require(text, "not a native Metal renderer", context)
        require(text, "platform_backend=native", context)
        require_any(text, ("comparison-only", "diagnostic infrastructure"), context)
        if release_path.name in HISTORICAL_ISSUE_73_RELEASES:
            require(text, "issue #73", f"{context} historical macOS limitation tracking")
        elif release_path.name in HISTORICAL_ISSUE_98_RELEASES:
            require(text, "issue #98", f"{context} historical macOS limitation tracking")
        require(text, "docs/dev/macos-signoff-evidence.md", context)

        lowered = text.lower()
        for phrase in promotion_phrases:
            if phrase not in lowered:
                continue
            require(text, "docs/dev/macos-signoff-evidence.md", f"{context} promotion evidence")
            require(text, "macOS Evidence Gate", f"{context} promotion evidence gate")


def validate_phase0_plan_status() -> None:
    plan = read(PLAN_PATH)

    for token in (
        "## Phase 0: Keep The Support Claim Honest",
        '- [x] Keep `README.md`, `BUILDING.md`, `docs/user/getting-started.md`,',
        "- [x] Keep Intel Mac, universal2, and Rosetta out of user-facing claims until a",
        '- [x] Keep `macos_graphics_bridge=metal` wording as "Metal bridge" everywhere.',
        "- [x] Keep `platform_backend=native` documented as comparison-only.",
        "- [x] Add a docs/static guard that fails if release notes promote macOS beyond",
        "- [x] Add release-note boilerplate for issue #73 until it is closed or",
        "Phase 0 implementation status",
        "`tools/tests/macos_support_claim_policy.py`",
    ):
        require(plan, token, "Phase 0 Apple support plan")


def validate_ci_and_local_wiring() -> None:
    local_runner = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")

    for source, context in (
        (local_runner, "local validation runner"),
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
    ):
        require(source, "macos_support_claim_policy.py", context)

    for source, context in (
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
    ):
        require(source, "python tools/tests/macos_support_claim_policy.py", context)


def main() -> None:
    validate_release_facing_docs()
    validate_arm64_tier_labels()
    validate_release_completion_guard()
    validate_curated_release_notes()
    validate_phase0_plan_status()
    validate_ci_and_local_wiring()
    print("macos_support_claim_policy: ok")


if __name__ == "__main__":
    main()
