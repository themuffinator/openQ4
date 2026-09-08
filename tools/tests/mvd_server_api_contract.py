#!/usr/bin/env python3
"""Static contract for the game-module-facing server MVD lifecycle API."""

from __future__ import annotations

import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT.parent / "openQ4-game")
).resolve()


def read(root: Path, relative: str) -> str:
    path = root / relative
    if not path.is_file():
        raise AssertionError(f"Required file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"Missing {token!r} in {context}")


def main() -> None:
    engine_network_h = read(ROOT, "src/framework/async/NetworkSystem.h")
    game_network_h = read(GAME_ROOT, "src/framework/async/NetworkSystem.h")
    network_cpp = read(ROOT, "src/framework/async/NetworkSystem.cpp")
    mvd_h = read(ROOT, "src/framework/async/MultiViewDemo.h")
    mvd_cpp = read(ROOT, "src/framework/async/MultiViewDemo.cpp")
    game_api_h = read(GAME_ROOT, "src/game/Game.h")

    declarations = (
        "ServerStartMVDRecording( const char *name )",
        "ServerStopMVDRecording( const char *reason )",
        "ServerIsMVDRecording( void ) const",
        "ServerCopyMVDRecordingQPath( char *buffer, int bufferSize ) const",
        "ServerCopyMVDRecordingResult( serverMVDRecordingResult_t &result ) const",
    )
    for declaration in declarations:
        require(engine_network_h, declaration, "engine NetworkSystem interface")
        require(game_network_h, declaration, "game NetworkSystem interface mirror")

    append_anchor = "RepeaterGetClientNum(int clientId)"
    for header, context in (
        (engine_network_h, "engine NetworkSystem interface"),
        (game_network_h, "game NetworkSystem interface mirror"),
    ):
        if any(header.index(item) <= header.index(append_anchor) for item in declarations):
            raise AssertionError(f"MVD lifecycle methods must remain append-only in {context}")

    for method in (
        "StartNamedRecording( const char *name )",
        "StopRecordingCleanly( const char *reason )",
        "CopyRecordingQPath( char *buffer, int bufferSize ) const",
        "CopyRecordingResult( serverMVDRecordingResult_t &result ) const",
    ):
        require(mvd_h, method, "public idMultiViewDemo lifecycle surface")

    require(mvd_cpp, "return StartRecording( args );", "named recording adapter")
    require(mvd_cpp, "fileName = BuildRecordingName( args );", "recording name allocation")
    require(
        mvd_cpp,
        "state != MVD_RECORDING || fileName.IsEmpty() || fileName.Length() >= bufferSize",
        "bounded recording qpath copy",
    )
    require(mvd_cpp, "idStr::Copynz( buffer, fileName.c_str(), bufferSize );", "bounded recording qpath copy")
    require(mvd_cpp, "return committed;", "clean recording finalization result")
    for token in (
        "SERVER_MVD_RESULT_PENDING",
        "SERVER_MVD_RESULT_COMMITTED",
        "SERVER_MVD_RESULT_FAILED",
        "finalQPath[ SERVER_MVD_RESULT_QPATH_BYTES + 1 ]",
        "partialQPath[ SERVER_MVD_RESULT_QPATH_BYTES + 1 ]",
    ):
        require(engine_network_h, token, "durable engine MVD result ABI")
        require(game_network_h, token, "durable game MVD result ABI mirror")
    require(mvd_cpp, "file->Sync()", "durable MVD stream sync before publication")
    require(mvd_cpp, "fileSystem->PromoteFile(", "validated atomic MVD publication")
    commit_start = mvd_cpp.index("bool idMultiViewDemo::CommitRecording()")
    commit_end = mvd_cpp.index("idMultiViewDemo::StopRecording", commit_start)
    if "rename(" in mvd_cpp[commit_start:commit_end]:
        raise AssertionError("MVD publication bypasses idFileSystem::PromoteFile")
    clear_start = mvd_cpp.index("void idMultiViewDemo::Clear()")
    clear_end = mvd_cpp.index("idMultiViewDemo::Init", clear_start)
    if "recordingResult" in mvd_cpp[clear_start:clear_end]:
        raise AssertionError("ordinary MVD Clear erased the durable terminal result")
    require(
        game_api_h,
        "// 42: durable server MVD publication results for competitive match evidence",
        "append-only MVD result ABI history",
    )
    require(game_api_h, "GAME_API_VERSION\t\t= 47", "current game-module ABI version")

    forwarding = (
        "multiViewDemo.StartNamedRecording( name )",
        "multiViewDemo.StopRecordingCleanly( reason )",
        "multiViewDemo.IsRecording()",
        "multiViewDemo.CopyRecordingQPath( buffer, bufferSize )",
        "multiViewDemo.CopyRecordingResult( result )",
    )
    for call in forwarding:
        require(network_cpp, call, "NetworkSystem MVD lifecycle forwarding")

    print("MVD server lifecycle API contract passed")


if __name__ == "__main__":
    main()
