#!/usr/bin/env python3
# Preserved from openQ4-game; Quake 4 SDK EULA applies. See docs/dev/game-test-coverage.md.
"""Contracts for the fs_savepath-only competition-series report adapter."""

from __future__ import annotations

import re
import shutil
import subprocess
import os
from pathlib import Path

from game_regression_suite import discover as discover_game_regressions


ROOT = Path(__file__).resolve().parents[2]
MATCH_DIR = ROOT / "src/mpgame/mp/match"
HEADER = MATCH_DIR / "MatchSeriesReportFileSystem.h"
SOURCE = MATCH_DIR / "MatchSeriesReportFileSystem.cpp"
STORAGE_HEADER = MATCH_DIR / "MatchSeriesReportStorage.h"
STORAGE_SOURCE = MATCH_DIR / "MatchSeriesReportStorage.cpp"
def _resolve_engine_root() -> Path:
    """Locate a sibling openQ4 checkout, if the caller has one.

    The directory is named "openQ4"; hardcoding "OpenQ4" only ever resolved on
    case-insensitive filesystems, so on Linux the engine-side assertions were
    skipped even when the engine was checked out beside this repository.
    """

    override = os.environ.get("OPENQ4_ENGINE_REPO", "").strip()
    if override:
        return Path(override)
    # The game sources live in the engine repository now, so this checkout is
    # the engine. Looking for a sibling named openQ4 first tested a different
    # checkout from a worktree, or none at all.
    if (ROOT / "src/framework").is_dir():
        return ROOT
    for name in ("openQ4", "OpenQ4"):
        candidate = ROOT.parent / name
        if candidate.is_dir():
            return candidate
    return ROOT.parent / "openQ4"


ENGINE_ROOT = _resolve_engine_root()


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r} in {context}")


def reject(text: str, token: str, context: str) -> None:
    if token in text:
        raise AssertionError(f"unexpected {token!r} in {context}")


def require_before(text: str, first: str, second: str, context: str) -> None:
    first_at = text.find(first)
    second_at = text.find(second)
    if first_at < 0 or second_at < 0 or first_at >= second_at:
        raise AssertionError(f"expected {first!r} before {second!r} in {context}")


def main() -> None:
    header = read(HEADER)
    source = read(SOURCE)
    storage_header = read(STORAGE_HEADER)
    storage_source = read(STORAGE_SOURCE)
    combined = header + source

    for token in (
        '#include "MatchSeriesReportStorage.h"',
        "class mpMatchSeriesReportFileSystemWriter",
        "public mpMatchSeriesReportStorageWriter",
        "idFileSystem *fileSystemBackend",
        "virtual int WriteTemp",
        "virtual bool Promote",
        "virtual bool RemoveTemp",
    ):
        require(header, token, "narrow production adapter API")

    for token in (
        'MP_MATCH_SERIES_REPORT_WRITABLE_ROOT = "fs_savepath"',
        "bytes >= MP_SERIES_REPORT_STORAGE_JSON_BYTES",
        "MPMatchSeriesReportStorageIsTemporaryQPath( temporaryQPath )",
        "OpenFileWrite( temporaryQPath",
        "file->Write( source + committedBytes, remaining )",
        "written <= 0 || written > remaining",
        "committedBytes == bytes && file->Sync()",
        "fileSystemBackend->CloseFile( file )",
        "MPMatchSeriesReportStorageIsPromotionPair( temporaryQPath, finalQPath )",
        "PromoteFile( temporaryQPath, finalQPath",
        "RemoveFileChecked( temporaryQPath",
    ):
        require(source, token, "bounded fs_savepath adapter")

    write_at = source.index("int mpMatchSeriesReportFileSystemWriter::WriteTemp")
    promote_at = source.index("bool mpMatchSeriesReportFileSystemWriter::Promote")
    remove_at = source.index("bool mpMatchSeriesReportFileSystemWriter::RemoveTemp")
    write_method = source[write_at:promote_at]
    promote_method = source[promote_at:remove_at]
    remove_method = source[remove_at:]
    require_before(
        write_method,
        "bytes >= MP_SERIES_REPORT_STORAGE_JSON_BYTES",
        "OpenFileWrite( temporaryQPath",
        "payload bound before opening a file",
    )
    require_before(
        write_method,
        "MPMatchSeriesReportStorageIsTemporaryQPath( temporaryQPath )",
        "OpenFileWrite( temporaryQPath",
        "canonical temporary qpath before write",
    )
    require_before(
        write_method,
        "file->Sync()",
        "CloseFile( file )",
        "durability attempt before close",
    )
    require_before(
        promote_method,
        "MPMatchSeriesReportStorageIsPromotionPair( temporaryQPath, finalQPath )",
        "PromoteFile( temporaryQPath, finalQPath",
        "exact promotion pair before atomic replacement",
    )
    require_before(
        remove_method,
        "MPMatchSeriesReportStorageIsTemporaryQPath( temporaryQPath )",
        "RemoveFileChecked( temporaryQPath",
        "canonical temporary qpath before cleanup",
    )

    # The adapter exposes no root selector, general read API, fallback copy, or
    # direct OS-path mutation.  The only accepted path language is implemented
    # by the storage core's exact canonical validators.
    for forbidden in (
        "WriteFile(",
        "ReadFile(",
        "OpenFileRead(",
        "RelativePathToOSPath",
        "RemoveFile(",
        "CopyFile",
        "MoveFile",
        "SDL_RenamePath",
        "rename(",
        "fopen(",
        "ofstream",
        "std::filesystem",
        "fs_cdpath",
        "fs_basepath",
        "system(",
        "popen(",
    ):
        reject(combined, forbidden, "exact-root report adapter")
    if source.count('"fs_savepath"') != 1:
        raise AssertionError("adapter must contain one immutable writable root")
    if re.search(r"(?:root|basePath|outputPath|callerPath)\s*[,) ]", header):
        raise AssertionError("adapter public API exposes caller-selected storage")

    for token in (
        '"match-results/series-"',
        "MPMatchSeriesReportStorageIsFinalQPath",
        "MPMatchSeriesReportStorageIsTemporaryQPath",
        "MPMatchSeriesReportStorageIsPromotionPair",
        "writer.WriteTemp",
        "writer.Promote",
    ):
        require(storage_header + storage_source, token,
                "server-owned atomic report storage core")

    filesystem_headers = [read(ROOT / "src/framework/FileSystem.h")]
    sibling_header = ENGINE_ROOT / "src/framework/FileSystem.h"
    if sibling_header.is_file():
        filesystem_headers.append(read(sibling_header))
    for filesystem_header in filesystem_headers:
        require(filesystem_header, "virtual bool\t\t\tPromoteFile(",
                "atomic filesystem promotion API")
        require(filesystem_header, "virtual bool\t\t\tRemoveFileChecked(",
                "checked temporary cleanup API")

    listing = subprocess.run(
        [
            shutil.which("python") or shutil.which("python3") or "python",
            str(ROOT / "tools/build/list_sources.py"),
            str(ROOT / "src"),
            "mpgame",
            "mpgame/Callbacks.cpp",
            "mpgame/gamesys/Callbacks.cpp",
        ],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.splitlines()
    if "mpgame/mp/match/MatchSeriesReportFileSystem.cpp" not in {
        line.strip() for line in listing
    }:
        raise AssertionError("series report filesystem adapter absent from MP sources")

    if "tools/tests/mp_match_series_report_filesystem_contract.py" not in discover_game_regressions():
        raise AssertionError("series report filesystem contract is absent from the game regression suite")
    print("mp_match_series_report_filesystem_contract: PASS")


if __name__ == "__main__":
    main()
