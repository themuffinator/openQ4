#!/usr/bin/env python3
"""Regression checks for release/version/docs tooling safety."""

from __future__ import annotations

import importlib.util
import argparse
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import uuid
import zlib
from pathlib import Path
from types import ModuleType


ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = ROOT / "tools" / "build"
WORK_BASE = ROOT / ".tmp" / "release-tooling-safety-test"


def make_work_root() -> Path:
    return WORK_BASE / f"{os.getpid()}-{uuid.uuid4().hex}"


WORK = make_work_root()


def load_module(name: str, path: Path) -> ModuleType:
    if str(path.parent) not in sys.path:
        sys.path.insert(0, str(path.parent))
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"could not load module from {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


CHANGELOG = load_module("release_changelog_under_test", BUILD_DIR / "generate_release_changelog.py")
DOCS = load_module("release_docs_under_test", BUILD_DIR / "generate_release_docs.py")
INSTALLER = load_module("windows_installer_under_test", BUILD_DIR / "build_windows_installer.py")
RELEASE_VERSION = load_module("release_version_under_test", BUILD_DIR / "openq4_release_version.py")
LINUX_RELEASE_ARTIFACTS = load_module(
    "linux_release_artifacts_under_test",
    BUILD_DIR / "verify_linux_release_artifacts.py",
)
SOURCE_PROVENANCE = load_module(
    "release_source_provenance_under_test",
    BUILD_DIR / "verify_release_source_provenance.py",
)
RELEASE_ASSET_SET = load_module(
    "release_asset_set_under_test",
    BUILD_DIR / "verify_release_asset_set.py",
)
SYNC_ICONS = load_module("sync_icons_under_test", BUILD_DIR / "sync_icons.py")
VERSION = load_module("openq4_version_under_test", BUILD_DIR / "openq4_version.py")
DOC_LINKS = load_module("docs_link_integrity_under_test", ROOT / "tools" / "tests" / "docs_link_integrity.py")


def write_file(path: Path, text: str = "data\n") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def make_symlink(target: Path, link: Path, *, target_is_directory: bool = False) -> bool:
    try:
        os.symlink(target, link, target_is_directory=target_is_directory)
    except (OSError, NotImplementedError):
        return False
    return True


def run(command: list[str], cwd: Path) -> str:
    completed = subprocess.run(
        command,
        cwd=cwd,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return completed.stdout.strip()


def git(repo: Path, *args: str) -> str:
    return run(["git", *args], repo)


def make_git_repo(path: Path) -> Path:
    path.mkdir(parents=True, exist_ok=True)
    git(path, "init", "-q")
    git(path, "config", "user.email", "openq4-tests@example.invalid")
    git(path, "config", "user.name", "openQ4 Tests")
    return path


def commit_file(repo: Path, name: str, text: str, subject: str) -> None:
    write_file(repo / name, text)
    git(repo, "add", name)
    git(repo, "commit", "-q", "-m", subject)


def expect_runtime_error(callback, text: str, label: str) -> None:
    try:
        callback()
    except RuntimeError as exc:
        if text not in str(exc):
            raise AssertionError(f"{label} raised {exc!r}, expected text {text!r}") from exc
        return
    raise AssertionError(f"{label} unexpectedly succeeded")


def expect_system_exit(callback, text: str, label: str) -> None:
    try:
        callback()
    except SystemExit as exc:
        if text not in str(exc):
            raise AssertionError(f"{label} raised {exc!r}, expected text {text!r}") from exc
        return
    raise AssertionError(f"{label} unexpectedly succeeded")


def expect_file_not_found(callback, text: str, label: str) -> None:
    try:
        callback()
    except FileNotFoundError as exc:
        if text not in str(exc):
            raise AssertionError(f"{label} raised {exc!r}, expected text {text!r}") from exc
        return
    raise AssertionError(f"{label} unexpectedly succeeded")


def expect_exception(callback, exception_type: type[BaseException], text: str, label: str) -> None:
    try:
        callback()
    except exception_type as exc:
        if text not in str(exc):
            raise AssertionError(f"{label} raised {exc!r}, expected text {text!r}") from exc
        return
    raise AssertionError(f"{label} unexpectedly succeeded")


def validate_changelog_git_context_and_limits() -> None:
    repo = make_git_repo(WORK / "release-repo")
    commit_file(repo, "file.txt", "one\n", "first")
    git(repo, "tag", "v0.1.001")
    commit_file(repo, "file.txt", "two\n", "second")
    git(repo, "tag", "v0.1.003")
    commit_file(repo, "file.txt", "three\n", "third")
    git(repo, "tag", "v0.2.000")
    commit_file(repo, "file.txt", "four\n", "fourth")

    top_level = CHANGELOG.run_git(repo, ["rev-parse", "--show-toplevel"])
    if Path(top_level).resolve() != repo.resolve():
        raise AssertionError("release changelog git helper ignored source_root")

    previous = CHANGELOG.find_previous_release_tag(repo, "v0.1.003")
    if previous != "v0.1.001":
        raise AssertionError(f"previous release tag should ignore newer tags, got {previous!r}")

    commits = CHANGELOG.collect_commits(repo, "v0.1.001..HEAD", 1)
    if len(commits) != 1:
        raise AssertionError(f"collect_commits did not enforce max_count for ranges: {len(commits)}")

    if CHANGELOG.collect_commits(repo, "v0.1.001..HEAD", 0):
        raise AssertionError("collect_commits should return no commits for max_count=0")


def validate_changelog_input_and_markdown_safety() -> None:
    expect_runtime_error(
        lambda: CHANGELOG.validate_repo_slug("owner/repo/extra"),
        "GitHub repository slug",
        "repository slug validation",
    )
    expect_runtime_error(
        lambda: CHANGELOG.validate_optional_https_url("http://example.invalid/run", "workflow run URL"),
        "https URL",
        "workflow URL validation",
    )
    expect_runtime_error(
        lambda: CHANGELOG.validate_optional_https_url("https://example.invalid/run) injected", "workflow run URL"),
        "unsafe in generated Markdown",
        "workflow URL markdown injection validation",
    )
    expect_runtime_error(
        lambda: CHANGELOG.validate_release_tag("release|bad"),
        "release tag",
        "release tag validation",
    )
    expect_runtime_error(
        lambda: CHANGELOG.validate_version_tag("0.1.010/bad"),
        "version tag",
        "version tag validation",
    )

    escaped = CHANGELOG.escape_markdown_inline("[link](javascript:alert(1)) | *bold*")
    for token in ("\\[link\\]", "\\(javascript:alert\\(1\\)\\)", "\\|", "\\*bold\\*"):
        if token not in escaped:
            raise AssertionError(f"markdown escape missed {token!r}: {escaped!r}")

    header = CHANGELOG.build_release_header(
        version="0.1.010",
        version_tag="0.1.010",
        release_tag="v0.1.010",
        release_scale="patch",
        release_reason="safe | injected\nnext row",
        repo_url="https://github.com/themuffinator/openQ4",
        head_sha="0123456789abcdef",
        generated_at="2026-06-23 00:00 UTC",
        run_id="run | id",
        run_url="https://github.com/themuffinator/openQ4/actions/runs/1",
        previous_tag="v0.1.009",
    )
    rendered = "\n".join(header)
    if "safe | injected" in rendered or "run | id" in rendered:
        raise AssertionError(f"release header did not escape table/link text:\n{rendered}")
    if "safe \\| injected next row" not in rendered or "run \\| id" not in rendered:
        raise AssertionError(f"release header lost escaped text:\n{rendered}")

    release_notes_body = CHANGELOG.sanitize_release_notes_override(
        "# openQ4 0.9.0 Release Notes\n\n## Highlights\n\n- Item\n",
        "0.9.0",
        "v0.9.0",
    )
    if release_notes_body != "## Highlights\n\n- Item":
        raise AssertionError(
            "release notes title should be removed before the generated header: "
            f"{release_notes_body!r}"
        )

    expect_exception(
        lambda: CHANGELOG.non_negative_int("-1"),
        argparse.ArgumentTypeError,
        "non-negative integer",
        "negative max commits",
    )
    expect_exception(
        lambda: CHANGELOG.non_negative_int("-1"),
        argparse.ArgumentTypeError,
        "non-negative integer",
        "negative max highlights",
    )


def validate_changelog_output_and_override_guards() -> None:
    output_root = WORK / "changelog-output"
    outside = WORK / "changelog-outside.md"
    write_file(outside, "outside\n")
    output_link = output_root / "release.md"
    output_link.parent.mkdir(parents=True, exist_ok=True)
    if make_symlink(outside, output_link):
        expect_runtime_error(
            lambda: CHANGELOG.validate_output_path(output_link),
            "through symlink",
            "release changelog output symlink guard",
        )

    parent_target = WORK / "changelog-parent-target"
    parent_target.mkdir(parents=True, exist_ok=True)
    parent_link = WORK / "changelog-parent-link"
    if make_symlink(parent_target, parent_link, target_is_directory=True):
        expect_runtime_error(
            lambda: CHANGELOG.validate_output_path(parent_link / "release.md"),
            "symlinked directory",
            "release changelog output parent symlink guard",
        )

    notes_root = WORK / "changelog-notes-source"
    release_notes_target = WORK / "external-release-notes.md"
    write_file(release_notes_target, "# external\n")
    release_notes_link = notes_root / "docs" / "dev" / "releases" / "v0.1.010.md"
    release_notes_link.parent.mkdir(parents=True, exist_ok=True)
    if make_symlink(release_notes_target, release_notes_link):
        expect_runtime_error(
            lambda: CHANGELOG.find_tracked_release_notes(notes_root, "v0.1.010", "0.1.010"),
            "must not be a symlink",
            "tracked release notes symlink guard",
        )


def validate_openq4_version_iteration() -> None:
    expect_system_exit(
        lambda: VERSION.resolve_auto_iteration_date("20260231"),
        "real UTC calendar date",
        "invalid auto iteration date",
    )

    original_run_git = VERSION.run_git
    VERSION.run_git = lambda _root, *_args: "\n".join(
        [
            "dev-0.1.010-dev.20260623.1",
            "dev-0.1.010-dev.20260623.10",
            "dev-0.1.010-dev.20260623.2",
            "dev-0.1.010-dev.20260623.preview",
        ]
    )
    try:
        iteration = VERSION.compute_auto_iteration(Path("."), "0.1.010", "dev", "20260623")
    finally:
        VERSION.run_git = original_run_git

    if iteration != "20260623.11":
        raise AssertionError(f"auto iteration should follow the highest suffix, got {iteration!r}")


def validate_version_header_build_refresh_contract() -> None:
    meson_text = (ROOT / "meson.build").read_text(encoding="utf-8")
    for token in (
        "generated_version_header = custom_target(",
        "version_metadata_command + ['--header-out', '@OUTPUT@']",
        "build_always_stale: true",
        "build_by_default: true",
        "openq4_engine_sources += [openq4_paks_generated_header, generated_version_header]",
        "game_sp_sources += [savegame_compat_header, generated_version_header]",
        "game_mp_sources += [savegame_compat_header, generated_version_header]",
        "openq4_dedicated_sources += [openq4_paks_generated_header, generated_version_header]",
    ):
        if token not in meson_text:
            raise AssertionError(f"version header build-refresh contract is missing {token!r}")

    if "version_metadata_command + ['--header-out', generated_version_header_path]" not in meson_text:
        raise AssertionError("configure-time version header bootstrap is missing")


def validate_release_version_floor_and_docs_classification() -> None:
    current = RELEASE_VERSION.parse_version("0.1.011")
    lower = RELEASE_VERSION.parse_version("0.1.010")
    latest = ("v0.1.012", RELEASE_VERSION.parse_version("0.1.012"))

    expect_system_exit(
        lambda: RELEASE_VERSION.validate_version_override(lower, current, None),
        "must not be lower than the configured project version",
        "version override current floor",
    )
    expect_system_exit(
        lambda: RELEASE_VERSION.validate_version_override(current, current, latest),
        "must not be lower than the latest published release",
        "version override latest floor",
    )

    category, weight, is_major = RELEASE_VERSION.classify_path("docs/user/getting-started.md")
    if (category, weight, is_major) != ("docs", 0, False):
        raise AssertionError(f"docs/user path was not classified as docs-only: {(category, weight, is_major)!r}")
    category, weight, is_major = RELEASE_VERSION.classify_path("assets/docs/img/banner.png")
    if (category, weight, is_major) != ("docs", 0, False):
        raise AssertionError(f"docs asset path was not classified as docs-only: {(category, weight, is_major)!r}")
    category, weight, is_major = RELEASE_VERSION.classify_path("assets/icons/quake4.ico")
    if (category, weight, is_major) != ("packaging", 2, False):
        raise AssertionError(f"packaged icon path was not classified as packaging: {(category, weight, is_major)!r}")
    category, weight, is_major = RELEASE_VERSION.classify_path("assets/linux/openQ4-steamdeck.in")
    if (category, weight, is_major) != ("platform", 2, False):
        raise AssertionError(f"Linux launcher asset path was not classified as platform: {(category, weight, is_major)!r}")

    scale, reason, score = RELEASE_VERSION.analyze_change_scale(
        ["docs/user/getting-started.md"],
        commit_count=1,
        additions=10,
        deletions=0,
    )
    if (scale, score) != ("patch", 0) or "Only documentation" not in reason:
        raise AssertionError(f"docs/user-only change should stay docs-only, got {(scale, reason, score)!r}")

    scale, reason, score = RELEASE_VERSION.analyze_change_scale(
        ["assets/icons/quake4.ico"],
        commit_count=1,
        additions=1,
        deletions=1,
    )
    if (scale, score) != ("patch", 2) or "Only documentation" in reason:
        raise AssertionError(f"packaged asset change should not be docs-only, got {(scale, reason, score)!r}")


def validate_release_docs_output_guard() -> None:
    source_root = WORK / "docs-source"
    write_file(source_root / "README.md", "# Docs\n\nBody\n")

    expect_runtime_error(
        lambda: DOCS.validate_output_root(source_root, source_root),
        "source root",
        "docs output source root guard",
    )
    expect_runtime_error(
        lambda: DOCS.validate_output_root(source_root, source_root.parent),
        "parent directories",
        "docs output parent guard",
    )
    expect_runtime_error(
        lambda: DOCS.validate_output_root(source_root, source_root / "docs/user"),
        "source-controlled",
        "docs output source-controlled guard",
    )

    DOCS.validate_output_root(source_root, source_root / ".tmp" / "docs")
    DOCS.validate_output_root(source_root, source_root / "builddir-ci-linux" / "docs")

    output_file = source_root / ".tmp" / "docs-file"
    write_file(output_file, "not a directory\n")
    expect_runtime_error(
        lambda: DOCS.validate_output_root(source_root, output_file),
        "not a directory",
        "docs output file guard",
    )

    output_target = source_root / ".tmp" / "docs-target"
    output_target.mkdir(parents=True, exist_ok=True)
    output_link = source_root / ".tmp" / "docs-link"
    if make_symlink(output_target, output_link, target_is_directory=True):
        expect_runtime_error(
            lambda: DOCS.validate_output_root(source_root, output_link),
            "symlinked output",
            "docs symlinked output guard",
        )

    source_target = WORK / "docs-source-target"
    source_target.mkdir(parents=True, exist_ok=True)
    source_link = WORK / "docs-source-link"
    if make_symlink(source_target, source_link, target_is_directory=True):
        expect_runtime_error(
            lambda: DOCS.validate_source_root(source_link),
            "source root must not be a symlink",
            "docs symlinked source root guard",
        )


def validate_release_docs_layout() -> None:
    source_root = WORK / "docs-layout-source"
    legacy_user_root = "docs" + "-user"
    legacy_dev_root = "docs" + "-dev"
    write_file(source_root / "README.md", "# Docs\n\nProject docs.\n")
    write_file(source_root / "docs" / "user" / "getting-started.md", "# Getting Started\n\nUser guide.\n")
    write_file(source_root / "docs" / "dev" / "platform-support.md", "# Platform Support\n\nDeveloper guide.\n")
    write_file(source_root / "docs" / "dev" / "proposals" / "renderer.md", "# Renderer Proposal\n\nResearch.\n")
    write_file(source_root / legacy_user_root / "legacy.md", "# Old User Path\n\nIgnored.\n")
    write_file(source_root / legacy_dev_root / "legacy.md", "# Old Dev Path\n\nIgnored.\n")

    sources = {relative.as_posix() for relative in DOCS.collect_doc_sources(source_root)}
    expected = {
        "README.md",
        "docs/user/getting-started.md",
        "docs/dev/platform-support.md",
        "docs/dev/proposals/renderer.md",
    }
    missing = expected - sources
    if missing:
        raise AssertionError(f"release docs did not collect new docs layout entries: {sorted(missing)!r}")
    unexpected_legacy = {f"{legacy_user_root}/legacy.md", f"{legacy_dev_root}/legacy.md"} & sources
    if unexpected_legacy:
        raise AssertionError(f"release docs still collect legacy docs layout entries: {sorted(unexpected_legacy)!r}")

    classifications = {
        "README.md": "Project",
        "docs/user/getting-started.md": "User Guides",
        "docs/dev/platform-support.md": "Developer Guides",
        "docs/dev/proposals/renderer.md": "Research and Proposals",
    }
    for relative, expected_group in classifications.items():
        group = DOCS.classify_group(Path(relative))
        if group != expected_group:
            raise AssertionError(f"{relative} classified as {group!r}, expected {expected_group!r}")


def validate_release_docs_symlink_and_link_guards() -> None:
    source_root = WORK / "docs-symlink-source"
    target = WORK / "docs-symlink-target.md"
    write_file(target, "# Leaked\n\nOutside content.\n")
    readme_link = source_root / "README.md"
    readme_link.parent.mkdir(parents=True, exist_ok=True)
    if make_symlink(target, readme_link):
        expect_runtime_error(
            lambda: DOCS.collect_doc_sources(source_root),
            "symlinked documentation source",
            "root documentation source symlink guard",
        )

    nested_root = WORK / "docs-nested-symlink-source"
    write_file(nested_root / "README.md", "# Docs\n\nBody\n")
    nested_link = nested_root / "docs" / "user" / "linked.md"
    nested_link.parent.mkdir(parents=True, exist_ok=True)
    if make_symlink(target, nested_link):
        expect_runtime_error(
            lambda: DOCS.collect_doc_sources(nested_root),
            "symlinked documentation source",
            "nested documentation source symlink guard",
        )

    asset_root = WORK / "docs-assets" / "img"
    asset_dest = WORK / "docs-assets-out"
    write_file(asset_root / "safe.png", "not really an image\n")
    asset_link = asset_root / "leak.png"
    if make_symlink(target, asset_link):
        expect_runtime_error(
            lambda: DOCS.copy_docs_asset_tree(asset_root, asset_dest),
            "symlinked documentation asset",
            "documentation asset symlink guard",
        )

    auxiliary_root = WORK / "docs-auxiliary-source"
    auxiliary_link = auxiliary_root / "LICENSE"
    auxiliary_link.parent.mkdir(parents=True, exist_ok=True)
    if make_symlink(target, auxiliary_link):
        expect_runtime_error(
            lambda: DOCS.copy_docs_auxiliary_file(auxiliary_root, Path("LICENSE"), WORK / "docs-auxiliary-out" / "LICENSE"),
            "symlinked documentation auxiliary file",
            "documentation auxiliary symlink guard",
        )

    prepared = DOCS.prepare_markdown(
        "[bad](javascript:alert(1)) "
        "![inline](data:text/html,evil) "
        "[file](file:///tmp/leak) "
        "[ok](https://example.invalid/docs)"
    )
    for unsafe in ("javascript:", "data:text", "file:///"):
        if unsafe in prepared:
            raise AssertionError(f"unsafe generated-docs URI scheme survived rewrite: {prepared!r}")
    if "[ok](https://example.invalid/docs)" not in prepared:
        raise AssertionError(f"safe HTTPS link was not preserved: {prepared!r}")


def validate_docs_link_integrity_work_roots() -> None:
    first = DOC_LINKS.make_work_root()
    second = DOC_LINKS.make_work_root()
    expected_parent = ROOT / ".tmp" / "docs-link-integrity"
    if first == second:
        raise AssertionError(f"docs link integrity work roots are not unique: {first}")
    if first.parent != expected_parent or second.parent != expected_parent:
        raise AssertionError(f"docs link integrity work roots are outside the expected temp root: {first}, {second}")
    if str(os.getpid()) not in first.name or str(os.getpid()) not in second.name:
        raise AssertionError(f"docs link integrity work roots do not include the process id: {first}, {second}")


def validate_docs_link_integrity_local_link_guards() -> None:
    source_root = WORK / "docs-link-integrity-source"
    outside = WORK / "outside-doc.md"
    write_file(outside, "# Outside\n")
    write_file(source_root / "README.md", "[outside](../outside-doc.md)\n")

    expect_exception(
        lambda: DOC_LINKS.validate_markdown_links_for_sources([source_root / "README.md"], source_root),
        AssertionError,
        "escapes documentation root",
        "docs link integrity escaped local Markdown link",
    )

    symlink_source_root = WORK / "docs-link-integrity-symlink-source"
    write_file(symlink_source_root / "README.md", "# Root\n")
    symlinked_doc = symlink_source_root / "docs" / "user" / "linked.md"
    symlinked_doc.parent.mkdir(parents=True, exist_ok=True)
    if make_symlink(outside, symlinked_doc):
        expect_exception(
            lambda: DOC_LINKS.markdown_sources(symlink_source_root),
            AssertionError,
            "symlinked Markdown source",
            "docs link integrity symlinked Markdown source",
        )

    symlinked_text = symlink_source_root / "docs" / "user" / "linked.txt"
    if make_symlink(outside, symlinked_text):
        expect_exception(
            lambda: DOC_LINKS.iter_text_files(symlink_source_root),
            AssertionError,
            "must not be a symlink",
            "docs link integrity symlinked text source",
        )


def validate_icon_sync_symlink_guards() -> None:
    icon_root = WORK / "icon-sync"
    target = icon_root / "real.ico"
    link = icon_root / "quake4.ico"
    write_file(target, "ico\n")
    if make_symlink(target, link):
        expect_file_not_found(
            lambda: SYNC_ICONS.ensure_file(link, "ICO icon"),
            "must not be a symlink",
            "sync_icons required file symlink guard",
        )

    png_target = icon_root / "real.png"
    png_link = icon_root / "quake4_1024.png"
    write_file(png_target, "png\n")
    if make_symlink(png_target, png_link):
        expect_file_not_found(
            lambda: SYNC_ICONS.highest_png_source(icon_root),
            "must not be a symlink",
            "sync_icons PNG source symlink guard",
        )

    output_target = icon_root / "output-target.png"
    output_link = icon_root / "quake4_16.png"
    write_file(output_target, "outside\n")
    if make_symlink(output_target, output_link):
        expect_runtime_error(
            lambda: SYNC_ICONS.ensure_writable_icon_output(output_link),
            "must not be a symlink",
            "sync_icons generated output symlink guard",
        )


def validate_windows_installer_payload_requirements() -> None:
    package_dir = WORK / "installer-package"
    required = [
        "openQ4-client_x64.exe",
        "openQ4-client_x64.pdb",
        "openQ4-ded_x64.exe",
        "openQ4-ded_x64.pdb",
        "README.html",
        "LICENSE",
        "docs/index.html",
        "baseoq4/mod.json",
        "baseoq4/pak0.pk4",
        "baseoq4/pak1.pk4",
        "baseoq4/game-sp_x64.pdb",
        "baseoq4/game-mp_x64.pdb",
    ]
    for relative in required:
        write_file(package_dir / relative)

    expect_file_not_found(
        lambda: INSTALLER.validate_package_dir(package_dir, "x64"),
        "OpenAL32.dll",
        "installer missing OpenAL runtime",
    )
    write_file(package_dir / "OpenAL32.dll")
    expect_file_not_found(
        lambda: INSTALLER.validate_package_dir(package_dir, "x64"),
        "game-sp_x64.dll",
        "installer missing game module DLLs",
    )
    write_file(package_dir / "baseoq4" / "game-sp_x64.dll")
    expect_file_not_found(
        lambda: INSTALLER.validate_package_dir(package_dir, "x64"),
        "game-mp_x64.dll",
        "installer missing multiplayer module DLL",
    )
    write_file(package_dir / "baseoq4" / "game-mp_x64.dll")
    INSTALLER.validate_package_dir(package_dir, "x64")


def validate_windows_installer_symlink_guards() -> None:
    package_dir = WORK / "installer-symlink-package"
    required = [
        "openQ4-client_x64.exe",
        "openQ4-client_x64.pdb",
        "openQ4-ded_x64.exe",
        "openQ4-ded_x64.pdb",
        "OpenAL32.dll",
        "README.html",
        "LICENSE",
        "docs/index.html",
        "baseoq4/mod.json",
        "baseoq4/pak0.pk4",
        "baseoq4/pak1.pk4",
        "baseoq4/game-sp_x64.dll",
        "baseoq4/game-sp_x64.pdb",
        "baseoq4/game-mp_x64.dll",
        "baseoq4/game-mp_x64.pdb",
    ]
    for relative in required:
        write_file(package_dir / relative)

    real_client = package_dir / "real-client.exe"
    write_file(real_client, "client\n")
    (package_dir / "openQ4-client_x64.exe").unlink()
    if make_symlink(real_client, package_dir / "openQ4-client_x64.exe"):
        expect_file_not_found(
            lambda: INSTALLER.validate_package_dir(package_dir, "x64"),
            "must not be symlinks",
            "installer payload symlink guard",
        )

    output_dir = WORK / "installer-output"
    script_path = output_dir / "openq4-0.1.010-windows-x64-setup.iss"
    installer_path = output_dir / "openq4-0.1.010-windows-x64-setup.exe"
    outside = WORK / "installer-output-outside.iss"
    write_file(outside, "outside\n")
    script_path.parent.mkdir(parents=True, exist_ok=True)
    if make_symlink(outside, script_path):
        expect_runtime_error(
            lambda: INSTALLER.validate_output_paths(output_dir, script_path, installer_path),
            "through symlink",
            "installer script output symlink guard",
        )

    output_target = WORK / "installer-output-target"
    output_target.mkdir(parents=True, exist_ok=True)
    output_link = WORK / "installer-output-link"
    if make_symlink(output_target, output_link, target_is_directory=True):
        expect_runtime_error(
            lambda: INSTALLER.validate_output_paths(
                output_link,
                output_link / "openq4-0.1.010-windows-x64-setup.iss",
                output_link / "openq4-0.1.010-windows-x64-setup.exe",
            ),
            "must not be a symlink",
            "installer output directory symlink guard",
        )

    package_target = WORK / "installer-package-target"
    package_target.mkdir(parents=True, exist_ok=True)
    package_link = WORK / "installer-package-link"
    if make_symlink(package_target, package_link, target_is_directory=True):
        expect_file_not_found(
            lambda: INSTALLER.require_directory_arg(package_link, "package directory", must_exist=True),
            "must not be a symlink",
            "installer raw package directory symlink guard",
        )

    asset_target = WORK / "installer-template-target.iss"
    asset_link = WORK / "installer-template-link.iss"
    write_file(asset_target, "; template\n")
    if make_symlink(asset_target, asset_link):
        expect_file_not_found(
            lambda: INSTALLER.require_regular_file(asset_link, "installer template"),
            "must not be a symlink",
            "installer template symlink guard",
        )


def validate_windows_installer_input_escaping() -> None:
    expect_exception(
        lambda: INSTALLER.validate_version("0.1/bad"),
        ValueError,
        "dotted numeric",
        "installer version validation",
    )
    expect_exception(
        lambda: INSTALLER.validate_filename_token("../escape", "version tag"),
        ValueError,
        "file-name-safe",
        "installer version tag validation",
    )

    rendered = INSTALLER.render_installer_script(
        '#define PackageSource "@@PACKAGE_SOURCE@@"\n@@OUTPUT_BASENAME@@\n',
        package_dir=Path('C:/release/openq4 "quoted"'),
        output_dir=Path("C:/out"),
        version="0.1.010",
        version_tag="0.1.010",
        arch="x64",
        setup_icon_file=Path("C:/icons/openQ4.ico"),
    )
    expected_source = INSTALLER.inno_string_contents(Path('C:/release/openq4 "quoted"'))
    if f'#define PackageSource "{expected_source}"' not in rendered:
        raise AssertionError(f"installer PackageSource was not escaped for Inno Setup:\n{rendered}")


def validate_discord_release_download_suffixes() -> None:
    announcer = (ROOT / ".github" / "scripts" / "announce-release-discord.mjs").read_text(encoding="utf-8")
    for suffix in (
        "macos-arm64-opengl.dmg",
        "macos-arm64-metal.dmg",
        "macos-arm64-opengl-unsigned.tar.gz",
        "macos-arm64-metal-unsigned.tar.gz",
    ):
        if suffix not in announcer:
            raise AssertionError(f"Discord release announcer is missing macOS download suffix {suffix!r}")
    for token in (
        '["linux-arm64.tar.xz", "Linux ARM64"]',
        'findAssetBySuffix(assets, "linux-arm64-preview.tar.xz")',
        'markdownLink("Linux ARM64 preview", linuxArm64PreviewAsset.browser_download_url)',
        "validateDiscordWebhookUrl",
        "validateGitHubRepoSlug",
        "validateHttpsUrl",
        "DISCORD_WEBHOOK_URL must point at a Discord webhook API path",
        "contains characters that are unsafe in Discord Markdown",
        "new Set([\"discord.com\", \"discordapp.com\"])",
        "new Set([\"github.com\"])",
    ):
        if token not in announcer:
            raise AssertionError(f"Discord release announcer is missing validation token {token!r}")


def validate_draft_releases_never_announce() -> None:
    """A draft has no tag, no audience, and must reach no Discord channel."""

    workflow = (ROOT / ".github" / "workflows" / "manual-release.yml").read_text(encoding="utf-8")
    announcer = (ROOT / ".github" / "scripts" / "announce-release-discord.mjs").read_text(encoding="utf-8")
    broadcast = (ROOT / ".github" / "workflows" / "discord-release.yml").read_text(encoding="utf-8")

    if "      draft:\n        description:" not in workflow:
        raise AssertionError("manual release workflow is missing the draft dispatch input")

    discord_offset = workflow.index("  discord:\n    name: Announce On Discord")
    discord_steps_offset = workflow.index("    steps:", discord_offset)
    if (
        "needs.release.result == 'success' && inputs.draft == false"
        not in workflow[discord_offset:discord_steps_offset]
    ):
        raise AssertionError("draft releases must skip the Discord announcement job")

    release_create_offset = workflow.index("- name: Create or update release")
    release_create_step = workflow[release_create_offset:discord_offset]
    for token in (
        "RELEASE_DRAFT: ${{ inputs.draft }}",
        'if [ "${RELEASE_DRAFT}" != "true" ] && [ "${RELEASE_DRAFT}" != "false" ]; then',
        "create_args+=(--draft)",
        '--json isDraft,targetCommitish',
        "was requested as a draft but reports isDraft=",
        "Draft release ${tag} records target",
    ):
        if token not in release_create_step:
            raise AssertionError(f"draft release publication is missing token: {token}")

    draft_branch_offset = release_create_step.index('if [ "${RELEASE_DRAFT}" = "true" ]; then\n            # A draft is not a release')
    publish_offset = release_create_step.index('-F draft=false')
    if draft_branch_offset > publish_offset:
        raise AssertionError("the draft branch must short-circuit before the release is published")
    if "exit 0" not in release_create_step[draft_branch_offset:publish_offset]:
        raise AssertionError("the draft branch must exit before clearing the draft flag")

    promotion = release_create_step[draft_branch_offset:publish_offset]
    if '--json databaseId --jq .databaseId' not in promotion:
        raise AssertionError("draft promotion must resolve the release ID through the draft-aware gh view")
    if 'releases/tags/${tag}' in promotion:
        raise AssertionError("draft promotion must not require a published release-by-tag lookup")
    if '-F prerelease=false' not in promotion:
        raise AssertionError("release publication must send typed boolean state to GitHub")

    if "if (release.draft) {" not in announcer:
        raise AssertionError("Discord release announcer must refuse to announce a draft")

    if "types: [published]" not in broadcast:
        raise AssertionError("the Discord broadcast workflow must only fire on published releases")
    for forbidden in ("created", "prereleased", "released", "edited"):
        if f"types: [{forbidden}" in broadcast:
            raise AssertionError(
                f"the Discord broadcast workflow must not fire on release '{forbidden}' events"
            )


def validate_validation_wiring() -> None:
    validator = (ROOT / "tools" / "validation" / "openq4_validate.py").read_text(encoding="utf-8")
    push = (ROOT / ".github" / "workflows" / "push-verification.yml").read_text(encoding="utf-8")
    commit = (ROOT / ".github" / "workflows" / "commit-validation.yml").read_text(encoding="utf-8")
    for context, text in (
        ("validation runner", validator),
        ("push workflow", push),
        ("commit workflow", commit),
    ):
        if "release_tooling_safety.py" not in text:
            raise AssertionError(f"release_tooling_safety.py is not wired into {context}")
        if "docs_link_integrity.py" not in text:
            raise AssertionError(f"docs_link_integrity.py is not wired into {context}")


def validate_manual_release_optimized_builds() -> None:
    workflow = (ROOT / ".github" / "workflows" / "manual-release.yml").read_text(encoding="utf-8")

    if '"--buildtype=debug",' in workflow or "--buildtype=debug " in workflow:
        raise AssertionError("manual release workflow must not package debug binaries")
    if '"--buildtype=debugoptimized",' not in workflow:
        raise AssertionError("Windows release packaging is not using the optimized diagnostics build type")
    posix_start = workflow.index("- name: Build and install (Linux/macOS)")
    posix_end = workflow.index("- name: Verify checked-out and staged source provenance", posix_start)
    posix_build = workflow[posix_start:posix_end]
    for token in (
        "setup_args=(",
        "--buildtype=debugoptimized",
        "-Db_ndebug=true",
        "--wrap-mode=forcefallback",
        'bash tools/build/meson_setup.sh "${setup_args[@]}"',
    ):
        if token not in posix_build:
            raise AssertionError(
                f"Linux/macOS release packaging is missing optimized build token: {token}"
            )
    if workflow.count("-Db_ndebug=true") < 2:
        raise AssertionError("manual release packaging must disable runtime asserts on Windows and POSIX release builds")

    gamelibs = (ROOT / "tools" / "build" / "build_gamelibs.ps1").read_text(encoding="utf-8")
    for token in (
        "[ValidateSet(\"plain\", \"debug\", \"debugoptimized\", \"release\", \"minsize\", \"custom\")]",
        "OPENQ4_GAMELIBS_BUILDTYPE",
        "\"release\"",
        "--buildtype=$BuildType",
    ):
        if token not in gamelibs:
            raise AssertionError(f"GameLibs helper is missing optimized-build token: {token}")


def validate_manual_release_linux_staged_gate() -> None:
    workflow = (ROOT / ".github" / "workflows" / "manual-release.yml").read_text(encoding="utf-8")
    validator = (ROOT / "tools" / "validation" / "openq4_validate.py").read_text(encoding="utf-8")

    # The gate covers Windows packages as well as Linux ones, but the ordering
    # invariant it enforces is Linux-specific: staged ELF/package validation
    # must see the binaries before debug symbols are split out of them.
    gate_name = "- name: Validate staged binaries and package layout (Linux/Windows)"
    symbols_name = "- name: Prepare Linux debug symbols"
    try:
        gate_offset = workflow.index(gate_name)
        symbols_offset = workflow.index(symbols_name)
    except ValueError as exc:
        raise AssertionError("manual release workflow is missing the Linux staged validation or symbol-split step") from exc

    if gate_offset >= symbols_offset:
        raise AssertionError("Linux staged ELF/package validation must run before debug-symbol splitting")

    gate = workflow[gate_offset:symbols_offset]
    for token in (
        "if: matrix.platform != 'macos'",
        "python tools/validation/openq4_validate.py push",
        "--skip-build",
        "--install",
        "--skip-python-tests",
    ):
        if token not in gate:
            raise AssertionError(f"Linux staged release gate is missing token: {token}")

    if "objcopy" in workflow[:symbols_offset]:
        raise AssertionError("manual release workflow mutates Linux binaries before staged validation")

    for token in (
        "validate_staged_architecture_set(",
        "validate_distinct_game_modules(root, sp_modules, mp_modules)",
        "validate_linux_binary_hardening(root, linux_binary_specs)",
        'fields[4] in {"GLOBAL", "WEAK", "UNIQUE"}',
        'fields[5] in {"DEFAULT", "PROTECTED"}',
        "len(public_symbols) == 1",
        'public_symbols[0][4] == "GetGameAPI"',
    ):
        if token not in validator:
            raise AssertionError(f"staged Linux release validation is missing required gate: {token}")

    skip_build_offset = validator.index("if not args.skip_build:")
    staged_validation_offset = validator.index("if args.install:\n            validate_staged_payload", skip_build_offset)
    runtime_offset = validator.index("if args.runtime:", staged_validation_offset)
    if staged_validation_offset >= runtime_offset:
        raise AssertionError("existing staged payload validation is not reachable with --skip-build --install")


def validate_manual_release_linux_post_mutation_gates() -> None:
    workflow = (ROOT / ".github" / "workflows" / "manual-release.yml").read_text(encoding="utf-8")
    verifier = (ROOT / "tools" / "build" / "verify_linux_release_artifacts.py").read_text(encoding="utf-8")

    for token in (
        'echo "debug_symbols_dir=${symbols_dir}" >> "$GITHUB_OUTPUT"',
        "- name: Validate stripped Linux ELFs and detached symbols",
        "python tools/build/verify_linux_release_artifacts.py split",
        '--runtime-root "${GITHUB_WORKSPACE}/.install"',
        '--symbols-root "${{ steps.linux_symbols.outputs.debug_symbols_dir }}"',
        "- name: Validate extracted Linux runtime archive",
        "python tools/build/verify_linux_release_artifacts.py archive",
        '--archive "${{ steps.package.outputs.archive_path }}"',
        '--symbols-root "${{ steps.linux_symbols.outputs.debug_symbols_dir }}"',
        '--work-root "${RUNNER_TEMP}"',
    ):
        if token not in workflow:
            raise AssertionError(f"manual release post-mutation Linux gate is missing token: {token}")

    pre_split = workflow.index("- name: Validate staged binaries and package layout (Linux/Windows)")
    split = workflow.index("- name: Prepare Linux debug symbols", pre_split)
    post_split = workflow.index("- name: Validate stripped Linux ELFs and detached symbols", split)
    package = workflow.index("- name: Prepare package", post_split)
    post_archive = workflow.index("- name: Validate extracted Linux runtime archive", package)
    upload = workflow.index("- name: Upload artifact", post_archive)
    if not pre_split < split < post_split < package < post_archive < upload:
        raise AssertionError(
            "manual release Linux gates must bracket symbol mutation and archive creation before upload"
        )

    for token in (
        "validate_linux_runtime_payload(runtime_root, args.arch)",
        "staged_validator.validate_staged_architecture_set(",
        "staged_validator.validate_distinct_game_modules(",
        "staged_validator.validate_linux_binary_hardening(",
        "staged_validator.validate_linux_client_runtime_dependencies(runtime_root, clients)",
        "staged_validator.validate_linux_dedicated_runtime_dependencies(",
        "[(dedicated[0], arch), (mp_modules[0], arch)]",
        "validate_linux_dependency_contract(runtime_root, binary_specs)",
        '[ldd, "-r", str(binary_path)]',
        'for tag in ("RPATH", "RUNPATH", "TEXTREL")',
        "FORBIDDEN_DIAGNOSTIC_LIB_RE",
        "LINUX_DEDICATED_ARCH_ALLOWED_NEEDED.get(",
        'dump_elf_section(path, ".note.gnu.build-id")',
        "GNU_DEBUGLINK_SECTION",
        "gnu_debuglink_crc32(debug_file)",
        "runtime_build_id != debug_build_id",
        "copied_elf_path",
        "MAX_ARCHIVE_ENTRIES",
        "MAX_ARCHIVE_MEMBER_BYTES",
        "MAX_ARCHIVE_TOTAL_BYTES",
        "extract_linux_runtime_archive(args.archive, extraction_root)",
        "validate_debuglink_pairs(runtime_root, args.symbols_root, binaries)",
        "tempfile.TemporaryDirectory(",
    ):
        if token not in verifier:
            raise AssertionError(f"Linux release artifact verifier is missing contract token: {token}")
    if "extractall(" in verifier or ".extract(" in verifier:
        raise AssertionError("Linux release archive verification must use bounded regular-file extraction")


def validate_manual_release_linux_runtime_gate() -> None:
    workflow = (ROOT / ".github" / "workflows" / "manual-release.yml").read_text(encoding="utf-8")
    renderer = (ROOT / "tools" / "tests" / "renderer_validation_matrix.py").read_text(encoding="utf-8")

    dependency_start = workflow.index("- name: Install Linux native dependencies")
    dependency_end = workflow.index("- name: Install macOS native dependencies", dependency_start)
    linux_dependencies = workflow[dependency_start:dependency_end]
    for package in (
        "libgl1-mesa-dri",
        "libglx-mesa0",
        "weston",
        "xauth",
        "xvfb",
    ):
        if package not in linux_dependencies:
            raise AssertionError(f"manual release Linux runtime dependencies are missing {package}")

    build_start = workflow.index("- name: Build and install (Linux/macOS)")
    build_end = workflow.index("- name: Verify checked-out and staged source provenance", build_start)
    posix_build = workflow[build_start:build_end]
    linux_sdl_block = (
        'if [ "${{ matrix.platform }}" = "linux" ]; then\n'
        '            setup_args+=("-Dlinux_x11=enabled")\n'
        "          fi"
    )
    if linux_sdl_block not in posix_build:
        raise AssertionError(
            "Linux release setup must opt into bundled SDL X11+Wayland without applying the option to macOS"
        )
    if workflow.count("-Dlinux_x11=enabled") != 1:
        raise AssertionError("manual release must scope its explicit linux_x11 option to the Linux setup block")

    post_split = workflow.index("- name: Validate stripped Linux ELFs and detached symbols")
    runtime_gate = workflow.index(
        "- name: Validate post-strip Linux release client under Wayland and X11", post_split
    )
    diagnostics = workflow.index("- name: Publish Linux release runtime diagnostics", runtime_gate)
    package = workflow.index("- name: Prepare package", diagnostics)
    if not post_split < runtime_gate < diagnostics < package:
        raise AssertionError(
            "Linux release compositor runtime validation must run after symbol stripping and before packaging"
        )

    runtime_step = workflow[runtime_gate:diagnostics]
    for token in (
        "if: matrix.platform == 'linux'",
        'find builddir -type f -name SDL_build_config.h',
        "builddir/subprojects/*/SDL_build_config.h",
        "Release SDL configuration did not come from the bundled fallback",
        "#define SDL_VIDEO_DRIVER_WAYLAND 1",
        "#define SDL_VIDEO_DRIVER_X11 1",
        "weston --backend=headless-backend.so",
        "--socket=openq4-release-wayland",
        "unset DISPLAY",
        "SDL_VIDEO_DRIVER=wayland",
        "SDL_VIDEODRIVER=wayland",
        "env -u WAYLAND_DISPLAY",
        "SDL_VIDEO_DRIVER=x11",
        "SDL_VIDEODRIVER=x11",
        "xvfb-run -a",
        "--cases sdl3-wayland-window-lifecycle",
        "--cases sdl3-x11-window-lifecycle",
        "--timeout 90",
        '--basepath ""',
        "--skip-official-pak-validation",
        'wayland_status=$?',
        'x11_status=$?',
    ):
        if token not in runtime_step:
            raise AssertionError(f"manual release Linux runtime gate is missing token: {token}")

    diagnostic_step = workflow[diagnostics:package]
    for token in (
        "if: ${{ failure() && matrix.platform == 'linux' }}",
        "actions/upload-artifact@v6",
        ".tmp/release-runtime/${{ matrix.binary_arch }}",
        "runtime-diagnostics",
        "include-hidden-files: true",
    ):
        if token not in diagnostic_step:
            raise AssertionError(f"manual release Linux runtime diagnostics are missing token: {token}")

    for token in (
        '"id": "sdl3-wayland-window-lifecycle"',
        '"id": "sdl3-x11-window-lifecycle"',
        '["SDL3: current video driver: wayland"]',
        '["SDL3: current video driver: x11"]',
        '["SDL3: graphics bridge: OpenGL"]',
        '["created OpenGL context"]',
        '["Shutting down OpenGL subsystem (SDL3 backend)"]',
        "executable = find_client_executable(root)",
        'runtime_dir = Path(args.runtime_dir).resolve() if args.runtime_dir else root / ".install"',
        'savepath = Path(args.savepath).resolve() if args.savepath else output_dir / "savepath"',
        "cwd=str(runtime_dir)",
        "exit_code == 0 and not timed_out",
    ):
        if token not in renderer:
            raise AssertionError(f"renderer runtime helper is missing release lifecycle contract: {token}")
    if 'else root / ".home"' in renderer:
        raise AssertionError("renderer validation must not inherit archived user settings by default")


def validate_manual_release_companion_checkout() -> None:
    workflow = (ROOT / ".github" / "workflows" / "manual-release.yml").read_text(encoding="utf-8")

    for token in (
        "OPENQ4_GAMELIBS_REPO: ${{ github.workspace }}/../openQ4-game",
        "openq4_game_ref:",
        "description: openQ4-game branch, tag, or full commit SHA to package",
        "openq4_source_sha: ${{ steps.source_revisions.outputs.openq4_source_sha }}",
        "openq4_game_ref: ${{ steps.source_revisions.outputs.openq4_game_ref }}",
        "openq4_game_sha: ${{ steps.source_revisions.outputs.openq4_game_sha }}",
        "linux_arm64_archive_sha256: ${{ steps.arm64_evidence.outputs.archive_sha256 }}",
        "ref: ${{ github.sha }}",
        "Publishing manual releases must be dispatched from refs/heads/main",
        "Linux ARM64 evidence candidates must be dispatched from a pushed branch",
        "must descend from the current origin/main",
        'git merge-base --is-ancestor "${openq4_source_sha}" refs/remotes/origin/main',
        'git -C "${resolution_repo}" fetch --quiet --no-tags --depth=1 origin "${input_ref}"',
        'openq4_game_sha="$(git -C "${resolution_repo}" rev-parse --verify \'FETCH_HEAD^{commit}\')"',
        "ref: ${{ needs.metadata.outputs.openq4_source_sha }}",
        'git -C "${OPENQ4_GAMELIBS_REPO}" fetch --quiet --no-tags --depth=1 origin "${expected_game_sha}"',
        'git -C "${OPENQ4_GAMELIBS_REPO}" checkout --quiet --detach "${expected_game_sha}"',
        "python tools/build/verify_release_source_provenance.py",
        '--expected-project-commit "${{ needs.metadata.outputs.openq4_source_sha }}"',
        '--expected-gamelibs-commit "${{ needs.metadata.outputs.openq4_game_sha }}"',
        "Release publication is bound to the triggering refs/heads/main commit",
        'git merge-base --is-ancestor "${OPENQ4_SOURCE_SHA}" refs/remotes/origin/main',
        'published_tag_sha="$(git rev-parse --verify "refs/tags/${tag}^{commit}")"',
        "published_release_target",
    ):
        if token not in workflow:
            raise AssertionError(f"manual release source pinning is missing token: {token}")

    if "OPENQ4_GAMELIBS_REPO: ${{ github.workspace }}/openQ4-game" in workflow:
        raise AssertionError(
            "manual release must not clone openQ4-game inside the engine checkout; "
            "that makes a clean engine source manifest appear dirty"
        )
    if "git clone --depth 1 https://github.com/themuffinator/openQ4-game.git" in workflow:
        raise AssertionError("manual release builds must not independently clone a moving openQ4-game ref")
    if workflow.count('--target "${target}"') != 2:
        raise AssertionError("manual release create and edit paths must both target the triggering openQ4 SHA")

    game_ref_input_offset = workflow.index("      openq4_game_ref:")
    game_ref_input_end = workflow.index("      linux_arm64_support_tier:", game_ref_input_offset)
    if "        default: main" not in workflow[game_ref_input_offset:game_ref_input_end]:
        raise AssertionError("manual release openq4_game_ref input must default to main")

    source_resolution_offset = workflow.index("- name: Resolve immutable source revisions")
    build_job_offset = workflow.index("  builds:")
    provenance_offset = workflow.index("- name: Verify checked-out and staged source provenance")
    staged_payload_offset = workflow.index("- name: Validate staged payload", provenance_offset)
    release_target_gate_offset = workflow.index("- name: Validate release candidate source and tag target")
    release_download_offset = workflow.index("- name: Download packaged artifacts", release_target_gate_offset)
    if source_resolution_offset >= build_job_offset:
        raise AssertionError("openQ4-game must be resolved once in metadata before matrix builds start")
    if provenance_offset >= staged_payload_offset:
        raise AssertionError("checked-out/staged source provenance must be verified before payload validation")
    if release_target_gate_offset >= release_download_offset:
        raise AssertionError("release candidate/tag targeting must be checked before artifacts are downloaded")

    for token in (
        "linux_arm64_support_tier:",
        "default: preview",
        'linux_arm64_support_tier == "first-class"',
        "linux_arm64_evidence_ref:",
        "generate_linux_arm64_evidence_candidate:",
        "Resolve accepted Linux ARM64 evidence",
        "docs/dev/linux-arm64-first-class-evidence.toml",
        "docs/dev/linux-arm64-evidence/stock-sp-report.json",
        "docs/dev/linux-arm64-evidence/dedicated-report.json",
        "verify_linux_arm64_release_evidence.py validate",
        "verify_linux_arm64_release_evidence.py verify",
        "verify_linux_arm64_release_evidence.py write-candidate",
        "verify_release_asset_set.py",
        "--print-archive-sha256",
        "--stock-sp-report",
        "--dedicated-report",
        "--expected-openq4-commit",
        "--expected-openq4-game-commit",
        "--expected-release-version",
        "--expected-version-tag",
        "--expected-release-tag",
        "--expected-package-filename",
        "Verify first-class Linux ARM64 evidence against staged and packaged bytes",
        "Write Linux ARM64 first-class evidence candidate",
        "Upload Linux ARM64 first-class evidence candidate",
        "Revalidate accepted Linux ARM64 archive after artifact download",
        "OPENQ4_ARM64_EVIDENCE_CANDIDATE",
        "Linux ARM64 first-class evidence candidate",
        "inputs.generate_linux_arm64_evidence_candidate == false",
        "SOURCE_DATE_EPOCH",
        "stale_user_preview_claims",
        "selected_curated_release_notes",
        "linux-arm64-preview.tar.xz",
        "linux-arm64-preview-debugsymbols.tar.xz",
        "## Linux ARM64 Support",
        "linux_arm64_preview_claim_pattern",
        "linux[[:space:]_-]+arm64",
        "linux_arm64_release_reason",
        "stale_linux_arm64_assets",
        "existing_release_assets",
        "gh release delete-asset",
        "Removing stale opposite-tier Linux ARM64 release asset",
    ):
        if token not in workflow:
            raise AssertionError(f"manual release Linux ARM64 tier gate is missing token: {token}")
    for forbidden in ("required_record_fields", "linux-arm64-signoff-status:start"):
        if forbidden in workflow:
            raise AssertionError(
                f"manual release must not trust legacy free-form ARM64 evidence token: {forbidden}"
            )

    evidence_resolution_offset = workflow.index("- name: Resolve accepted Linux ARM64 evidence")
    if not source_resolution_offset < evidence_resolution_offset < build_job_offset:
        raise AssertionError("accepted ARM64 evidence must be resolved and candidate-checked in metadata")
    package_offset = workflow.index("- name: Prepare package")
    archive_gate_offset = workflow.index("- name: Validate extracted Linux runtime archive", package_offset)
    evidence_gate_offset = workflow.index(
        "- name: Verify first-class Linux ARM64 evidence against staged and packaged bytes",
        archive_gate_offset,
    )
    artifact_upload_offset = workflow.index("- name: Upload artifact", evidence_gate_offset)
    if not package_offset < archive_gate_offset < evidence_gate_offset < artifact_upload_offset:
        raise AssertionError(
            "first-class ARM64 byte evidence must pass after archive validation and before artifact upload"
        )

    candidate_manifest_offset = workflow.index(
        "- name: Write Linux ARM64 first-class evidence candidate"
    )
    candidate_upload_offset = workflow.index(
        "- name: Upload Linux ARM64 first-class evidence candidate",
        candidate_manifest_offset,
    )
    generic_upload_offset = workflow.index("- name: Upload artifact", candidate_upload_offset)
    if not archive_gate_offset < candidate_manifest_offset < candidate_upload_offset < generic_upload_offset:
        raise AssertionError(
            "ARM64 candidate manifest/upload must consume the already-validated final archive"
        )
    candidate_step = workflow[candidate_manifest_offset:candidate_upload_offset]
    for token in (
        'candidate_archive="${{ steps.package.outputs.archive_path }}"',
        'candidate_package_dir="${{ steps.package.outputs.package_dir }}"',
        "verify_linux_arm64_release_evidence.py write-candidate",
    ):
        if token not in candidate_step:
            raise AssertionError(
                f"ARM64 candidate generation must bind the verified package output: {token}"
            )
    if "package_release.py" in candidate_step:
        raise AssertionError("ARM64 evidence-candidate generation must not rebuild the package")

    candidate_upload_step = workflow[candidate_upload_offset:generic_upload_offset]
    if "name: openq4-linux-arm64-first-class-evidence-candidate-" not in candidate_upload_step:
        raise AssertionError("ARM64 candidate must use a distinct non-release artifact name")
    generic_upload_end = workflow.index("- name: Upload macOS dSYM symbols", generic_upload_offset)
    if (
        "if: inputs.generate_linux_arm64_evidence_candidate == false"
        not in workflow[generic_upload_offset:generic_upload_end]
    ):
        raise AssertionError("ARM64 evidence-candidate runs must skip generic release artifacts")

    discord_offset = workflow.index("- name: Validate Discord webhook secret")
    release_matrix_offset = workflow.index("- name: Build release matrix", discord_offset)
    if (
        "if: inputs.generate_linux_arm64_evidence_candidate == false"
        not in workflow[discord_offset:release_matrix_offset]
    ):
        raise AssertionError("ARM64 evidence-candidate runs must not require the release webhook")

    release_job_offset = workflow.index("  release:")
    release_steps_offset = workflow.index("    steps:", release_job_offset)
    release_header = workflow[release_job_offset:release_steps_offset]
    if (
        "needs.builds.result == 'success'"
        " && inputs.generate_linux_arm64_evidence_candidate == false"
        not in release_header
    ):
        raise AssertionError("ARM64 evidence-candidate runs must skip GitHub release publication")

    release_download_offset = workflow.index(
        "- name: Download packaged artifacts", release_job_offset
    )
    release_asset_gate_offset = workflow.index(
        "- name: Verify platform archives", release_download_offset
    )
    release_hash_gate_offset = workflow.index(
        "- name: Revalidate accepted Linux ARM64 archive after artifact download",
        release_asset_gate_offset,
    )
    release_create_offset = workflow.index(
        "- name: Create or update release", release_hash_gate_offset
    )
    if not (
        release_download_offset
        < release_asset_gate_offset
        < release_hash_gate_offset
        < release_create_offset
    ):
        raise AssertionError(
            "downloaded release assets must pass exact-set and accepted ARM64 hash gates "
            "before publication"
        )

    asset_gate = workflow[release_asset_gate_offset:release_hash_gate_offset]
    for token in (
        "tools/build/verify_release_asset_set.py",
        'expected_args+=(--expected "${package_file}")',
        '--artifact-dir "${artifact_dir}"',
        '--manifest "${asset_manifest}"',
        'echo "asset_manifest=${asset_manifest}" >> "${GITHUB_OUTPUT}"',
    ):
        if token not in asset_gate:
            raise AssertionError(
                f"release artifact exact-set whitelist is missing token: {token}"
            )

    hash_gate = workflow[release_hash_gate_offset:release_create_offset]
    for token in (
        "if: needs.metadata.outputs.linux_arm64_support_tier == 'first-class'",
        "EXPECTED_ARM64_ARCHIVE_SHA256: ${{ needs.metadata.outputs.linux_arm64_archive_sha256 }}",
        "sha256sum --",
        "does not match accepted evidence",
    ):
        if token not in hash_gate:
            raise AssertionError(
                f"post-download ARM64 archive hash gate is missing token: {token}"
            )

    release_create_end = workflow.index("  discord:", release_create_offset)
    release_create_step = workflow[release_create_offset:release_create_end]
    for token in (
        "RELEASE_ASSET_MANIFEST: ${{ steps.release_assets.outputs.asset_manifest }}",
        'mapfile -t asset_names < "${RELEASE_ASSET_MANIFEST}"',
        'assets+=("${asset_path}")',
        "Existing release contains an unapproved asset",
        "Published release asset count does not match the approved whitelist",
        "Published release contains an unapproved asset",
        "Published release is missing approved asset",
    ):
        if token not in release_create_step:
            raise AssertionError(
                f"release publication does not consume only the verified whitelist: {token}"
            )
    if 'find "${assets_dir}"' in release_create_step:
        raise AssertionError("release publication must not upload a directory-wide find result")

    cleanup_offset = workflow.index("stale_linux_arm64_assets")
    edit_offset = workflow.index('gh release edit "${tag}"', cleanup_offset)
    upload_offset = workflow.index('gh release upload "${tag}"', edit_offset)
    if cleanup_offset >= edit_offset or edit_offset >= upload_offset:
        raise AssertionError(
            "manual release must remove opposite-tier Linux ARM64 assets before editing/uploading"
        )


def validate_release_source_provenance_verifier() -> None:
    root = WORK / "source-provenance"
    project = make_git_repo(root / "openQ4")
    gamelibs = make_git_repo(root / "openQ4-game")
    commit_file(project, ".gitignore", ".tmp/\n", "project source")
    commit_file(gamelibs, "game-source.txt", "game\n", "game source")
    project_sha = git(project, "rev-parse", "HEAD")
    gamelibs_sha = git(gamelibs, "rev-parse", "HEAD")
    manifest_path = project / ".tmp" / "gamelibs_stage" / "openq4_gamelibs_stage_manifest.json"
    manifest = {
        "format": 1,
        "projectGitCommit": project_sha,
        "projectGitDirty": False,
        "gameLibsGitCommit": gamelibs_sha,
        "gameLibsGitDirty": False,
        "fileCount": 0,
        "files": [],
    }
    write_file(manifest_path, json.dumps(manifest) + "\n")

    SOURCE_PROVENANCE.verify_release_source_provenance(
        project,
        gamelibs,
        manifest_path,
        project_sha,
        gamelibs_sha,
    )
    expect_runtime_error(
        lambda: SOURCE_PROVENANCE.verify_release_source_provenance(
            project,
            gamelibs,
            manifest_path,
            "0" * 40,
            gamelibs_sha,
        ),
        "does not match expected release commit",
        "mismatched checked-out project SHA",
    )

    manifest["gameLibsGitCommit"] = "f" * 40
    write_file(manifest_path, json.dumps(manifest) + "\n")
    expect_runtime_error(
        lambda: SOURCE_PROVENANCE.verify_release_source_provenance(
            project,
            gamelibs,
            manifest_path,
            project_sha,
            gamelibs_sha,
        ),
        "staged openQ4-game commit",
        "mismatched staged GameLibs SHA",
    )

    manifest["gameLibsGitCommit"] = gamelibs_sha
    write_file(manifest_path, json.dumps(manifest) + "\n")
    write_file(gamelibs / "game-source.txt", "dirty\n")
    expect_runtime_error(
        lambda: SOURCE_PROVENANCE.verify_release_source_provenance(
            project,
            gamelibs,
            manifest_path,
            project_sha,
            gamelibs_sha,
        ),
        "checkout is dirty",
        "dirty checked-out GameLibs source",
    )


def validate_linux_release_artifact_helper_contracts() -> None:
    root = WORK / "linux-release-artifacts"
    root.mkdir(parents=True, exist_ok=True)
    for diagnostic_library in ("libasan.so.8", "libubsan.so.1", "libclang_rt.tsan-x86_64.so"):
        if LINUX_RELEASE_ARTIFACTS.FORBIDDEN_DIAGNOSTIC_LIB_RE.search(diagnostic_library) is None:
            raise AssertionError(f"diagnostic dependency was not rejected: {diagnostic_library}")

    binary_path = root / "openQ4-ded_x64"
    binary_path.write_bytes(b"synthetic ELF fixture")
    original_readelf_output = LINUX_RELEASE_ARTIFACTS.staged_validator.readelf_output
    original_which = LINUX_RELEASE_ARTIFACTS.shutil.which
    original_run = LINUX_RELEASE_ARTIFACTS.subprocess.run
    dynamic_section = ""

    def fake_readelf_output(path: Path, args: list[str], source_root: Path) -> str:
        if path != binary_path or args != ["-W", "-d"] or source_root != root:
            raise AssertionError("unexpected release dependency readelf request")
        return dynamic_section

    def fake_which(program: str) -> str | None:
        return "/usr/bin/ldd" if program == "ldd" else original_which(program)

    def fake_run(command, **kwargs):
        if command != ["/usr/bin/ldd", "-r", str(binary_path)]:
            raise AssertionError(f"unexpected release dependency command: {command!r}")
        return subprocess.CompletedProcess(command, 0, stdout="", stderr="")

    try:
        LINUX_RELEASE_ARTIFACTS.staged_validator.readelf_output = fake_readelf_output
        LINUX_RELEASE_ARTIFACTS.shutil.which = fake_which
        LINUX_RELEASE_ARTIFACTS.subprocess.run = fake_run
        dynamic_section = """
 0x0000000000000001 (NEEDED)             Shared library: [ld-linux-x86-64.so.2]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
"""
        LINUX_RELEASE_ARTIFACTS.validate_linux_dependency_contract(
            root,
            [(binary_path, "x64", False)],
        )

        dynamic_section = """
 0x0000000000000001 (NEEDED)             Shared library: [ld-linux-aarch64.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
"""
        expect_runtime_error(
            lambda: LINUX_RELEASE_ARTIFACTS.validate_linux_dependency_contract(
                root,
                [(binary_path, "x64", False)],
            ),
            "ld-linux-aarch64.so.1",
            "cross-architecture release loader dependency",
        )
    finally:
        LINUX_RELEASE_ARTIFACTS.staged_validator.readelf_output = original_readelf_output
        LINUX_RELEASE_ARTIFACTS.shutil.which = original_which
        LINUX_RELEASE_ARTIFACTS.subprocess.run = original_run

    filename = "openQ4-client_x64.debug"
    crc = 0x12345678
    encoded_name = filename.encode("ascii") + b"\0"
    padded_name = encoded_name + b"\0" * ((4 - len(encoded_name) % 4) % 4)
    parsed_filename, parsed_crc = LINUX_RELEASE_ARTIFACTS.parse_gnu_debuglink_section(
        padded_name + crc.to_bytes(4, byteorder="little"), "little"
    )
    if (parsed_filename, parsed_crc) != (filename, crc):
        raise AssertionError("GNU debuglink parser did not preserve filename/CRC")
    expect_runtime_error(
        lambda: LINUX_RELEASE_ARTIFACTS.parse_gnu_debuglink_section(
            b"../escape.debug\0\0" + crc.to_bytes(4, byteorder="little"), "little"
        ),
        "unsafe filename",
        "unsafe GNU debuglink filename",
    )

    build_id_bytes = bytes.fromhex("0123456789abcdef0123456789abcdef01234567")
    build_id_note = (
        (4).to_bytes(4, "little")
        + len(build_id_bytes).to_bytes(4, "little")
        + (3).to_bytes(4, "little")
        + b"GNU\0"
        + build_id_bytes
    )
    if LINUX_RELEASE_ARTIFACTS.parse_gnu_build_id_note(build_id_note, "little") != build_id_bytes.hex():
        raise AssertionError("GNU build-ID parser did not preserve the note description")

    debug_file = root / filename
    debug_file.write_bytes(b"detached symbols\0\xff")
    expected_crc = zlib.crc32(debug_file.read_bytes()) & 0xFFFFFFFF
    if LINUX_RELEASE_ARTIFACTS.gnu_debuglink_crc32(debug_file) != expected_crc:
        raise AssertionError("GNU debuglink CRC helper disagrees with zlib CRC32")

    package_name = "openq4-1.2.3-linux-x64"
    archive_path = root / f"{package_name}.tar.xz"
    payload = b"version metadata\n"
    with tarfile.open(archive_path, "w:xz") as archive:
        info = tarfile.TarInfo(f"{package_name}/VERSION.txt")
        info.size = len(payload)
        info.mode = 0o644
        archive.addfile(info, io.BytesIO(payload))
    extracted = LINUX_RELEASE_ARTIFACTS.extract_linux_runtime_archive(
        archive_path, root / "valid-extract"
    )
    if (extracted / "VERSION.txt").read_bytes() != payload:
        raise AssertionError("bounded Linux archive extractor changed regular-file bytes")

    unsafe_archive = root / "openq4-1.2.4-linux-x64.tar.xz"
    with tarfile.open(unsafe_archive, "w:xz") as archive:
        info = tarfile.TarInfo("openq4-1.2.4-linux-x64/../escape")
        info.size = len(payload)
        info.mode = 0o644
        archive.addfile(info, io.BytesIO(payload))
    expect_runtime_error(
        lambda: LINUX_RELEASE_ARTIFACTS.extract_linux_runtime_archive(
            unsafe_archive, root / "unsafe-extract"
        ),
        "escapes its root",
        "Linux runtime archive traversal",
    )
    if (root / "escape").exists():
        raise AssertionError("unsafe Linux archive extraction wrote outside its isolated root")


def validate_release_asset_set_helper_contracts() -> None:
    root = WORK / "release-asset-set"
    artifact_dir = root / "artifacts"
    artifact_dir.mkdir(parents=True)
    expected = [
        "openq4-1.2.3-linux-x64.tar.xz",
        "openq4-1.2.3-linux-arm64.tar.xz",
        "openq4-1.2.3-windows-x64.zip",
    ]
    for name in expected:
        write_file(artifact_dir / name, name)

    verified = RELEASE_ASSET_SET.verify_asset_set(artifact_dir, expected)
    if verified != tuple(expected):
        raise AssertionError("release asset whitelist changed its approved ordering")
    manifest = root / "approved-assets.txt"
    RELEASE_ASSET_SET.write_asset_manifest(manifest, verified)
    if manifest.read_text(encoding="utf-8").splitlines() != expected:
        raise AssertionError("release asset manifest does not contain the exact whitelist")
    expect_runtime_error(
        lambda: RELEASE_ASSET_SET.write_asset_manifest(manifest, verified),
        "must not already exist",
        "existing release asset manifest output",
    )

    extra = artifact_dir / "openq4-1.2.3-unapproved.txt"
    write_file(extra)
    expect_runtime_error(
        lambda: RELEASE_ASSET_SET.verify_asset_set(artifact_dir, expected),
        "unexpected:",
        "unexpected release artifact",
    )
    extra.unlink()

    missing = artifact_dir / expected[0]
    missing.unlink()
    expect_runtime_error(
        lambda: RELEASE_ASSET_SET.verify_asset_set(artifact_dir, expected),
        "missing:",
        "missing approved release artifact",
    )
    write_file(missing)

    directory_entry = artifact_dir / "openq4-1.2.3-unapproved-directory"
    directory_entry.mkdir()
    expect_runtime_error(
        lambda: RELEASE_ASSET_SET.verify_asset_set(artifact_dir, expected),
        "non-regular:",
        "release artifact directory entry",
    )
    directory_entry.rmdir()

    symlink = artifact_dir / "openq4-1.2.3-unapproved-link"
    if make_symlink(artifact_dir / expected[0], symlink):
        expect_runtime_error(
            lambda: RELEASE_ASSET_SET.verify_asset_set(artifact_dir, expected),
            "non-regular:",
            "symlinked release artifact",
        )
        symlink.unlink()

    expect_runtime_error(
        lambda: RELEASE_ASSET_SET.verify_asset_set(artifact_dir, expected + [expected[0]]),
        "duplicate names",
        "duplicate release artifact whitelist entry",
    )
    expect_runtime_error(
        lambda: RELEASE_ASSET_SET.verify_asset_set(
            artifact_dir, ["../openq4-escape.tar.xz"]
        ),
        "unsafe expected release artifact name",
        "path-like release artifact whitelist entry",
    )


def main() -> None:
    shutil.rmtree(WORK, ignore_errors=True)
    try:
        validate_changelog_git_context_and_limits()
        validate_changelog_input_and_markdown_safety()
        validate_changelog_output_and_override_guards()
        validate_openq4_version_iteration()
        validate_version_header_build_refresh_contract()
        validate_release_version_floor_and_docs_classification()
        validate_release_docs_output_guard()
        validate_release_docs_layout()
        validate_release_docs_symlink_and_link_guards()
        validate_docs_link_integrity_work_roots()
        validate_docs_link_integrity_local_link_guards()
        validate_icon_sync_symlink_guards()
        validate_windows_installer_payload_requirements()
        validate_windows_installer_symlink_guards()
        validate_windows_installer_input_escaping()
        validate_discord_release_download_suffixes()
        validate_draft_releases_never_announce()
        validate_validation_wiring()
        validate_manual_release_optimized_builds()
        validate_manual_release_linux_staged_gate()
        validate_manual_release_linux_post_mutation_gates()
        validate_manual_release_linux_runtime_gate()
        validate_manual_release_companion_checkout()
        validate_release_source_provenance_verifier()
        validate_linux_release_artifact_helper_contracts()
        validate_release_asset_set_helper_contracts()
    finally:
        shutil.rmtree(WORK, ignore_errors=True)
    print("release_tooling_safety: ok")


if __name__ == "__main__":
    main()
