#!/usr/bin/env python3
"""Regression checks for openQ4 pure-pack and download handling."""

from __future__ import annotations

import importlib.util
import hashlib
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from types import ModuleType


ROOT = Path(__file__).resolve().parents[2]
SIZE_T_MAX = (1 << (struct.calcsize("P") * 8)) - 1


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    depth = 0
    for index in range(start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find end of function {signature!r}")


def require_order(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index == -1 or second_index == -1:
        raise AssertionError(f"Missing ordered symbols {first!r} and/or {second!r} in {context}")
    if first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def load_package_module() -> ModuleType:
    package_path = ROOT / "tools" / "build" / "package_nightly.py"
    spec = importlib.util.spec_from_file_location("package_nightly_for_openq4_pure_pack_test", package_path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"Could not load package module from {package_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_test_file(path: Path, contents: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(contents)


def expect_runtime_error(message: str, callback, context: str) -> None:
    try:
        callback()
    except RuntimeError as exc:
        if message not in str(exc):
            raise AssertionError(f"Expected {message!r} in {context}, got {exc!r}") from exc
        return
    raise AssertionError(f"Expected RuntimeError for {context}")


def is_safe_download_path_model(path: str) -> bool:
    if not path or path.startswith(("/", "\\")) or "\\" in path or ":" in path:
        return False
    if any(ord(character) < 32 or character in '<>"|?*' for character in path):
        return False
    segments = path.split("/")
    if not all(
        segment not in ("", ".", "..")
        and not segment.startswith(" ")
        and not segment.endswith((".", " "))
        and not is_windows_device_path_segment_model(segment)
        for segment in segments
    ):
        return False
    return len(segments[-1]) > 4 and segments[-1].lower().endswith(".pk4")


def is_windows_device_path_segment_model(segment: str) -> bool:
    stem = segment.split(".", 1)[0].lower()
    if stem in ("con", "prn", "aux", "nul"):
        return True
    return len(stem) == 4 and stem[:3] in ("com", "lpt") and stem[3] in "123456789¹²³"


def bounded_download_write_model(
    bytes_written: int,
    size: int,
    nmemb: int,
    maximum_bytes: int,
) -> int | None:
    if size and nmemb > SIZE_T_MAX // size:
        return None
    byte_count = size * nmemb
    if bytes_written > SIZE_T_MAX - byte_count:
        return None
    if maximum_bytes and (
        bytes_written > maximum_bytes
        or byte_count > maximum_bytes - bytes_written
    ):
        return None
    return bytes_written + byte_count


def validate_filesystem_pure_pack_contract() -> None:
    source = read("src/framework/FileSystem.cpp")
    game_dir_policy = read("src/framework/GameDirPolicy.h")
    file_system_header = read("src/framework/FileSystem.h")
    md5_header = read("src/idlib/hashing/MD5.h")
    md5_source = read("src/idlib/hashing/MD5.cpp")
    async_client = read("src/framework/async/AsyncClient.cpp")
    async_client_header = read("src/framework/async/AsyncClient.h")
    async_network_header = read("src/framework/async/AsyncNetwork.h")
    async_server = read("src/framework/async/AsyncServer.cpp")
    licensee_header = read("src/framework/licensee.h")
    session_menu = read("src/framework/Session_menu.cpp")
    curl_write = function_body(
        source,
        "size_t idFileSystemLocal::CurlWriteFunction(",
    )
    curl_progress = function_body(
        source,
        "int idFileSystemLocal::CurlProgressFunction(",
    )
    download_progress_box = function_body(
        session_menu,
        "void idSessionLocal::DownloadProgressBox(",
    )
    background_download = function_body(source, "dword BackgroundDownloadThread(")
    download_request = function_body(
        async_server,
        "void idAsyncServer::ProcessDownloadRequestMessage(",
    )
    download_path_helper = function_body(
        async_client,
        "static bool AsyncClient_IsSafeDownloadPath(",
    )
    download_device_helper = function_body(
        async_client,
        "static bool AsyncClient_IsWindowsDevicePathSegment(",
    )
    download_file_cleanup = function_body(
        async_client,
        "static void AsyncClient_CloseBackgroundDownloadFile(",
    )
    download_info = function_body(
        async_client,
        "void idAsyncClient::ProcessDownloadInfoMessage(",
    )
    download_handler = function_body(
        async_client,
        "void idAsyncClient::HandleDownloads(",
    )
    version_message = function_body(
        async_client,
        "void idAsyncClient::ProcessVersionMessage(",
    )
    version_check = function_body(
        async_client,
        "void idAsyncClient::SendVersionCheck(",
    )
    helper = function_body(source, "bool idFileSystemLocal::IsOpenQ4PurePack(")
    checksum_validator = function_body(source, "bool idFileSystemLocal::ValidateOpenQ4Paks(")
    misplaced_validator = function_body(source, "bool idFileSystemLocal::FindMisplacedOfficialPaks(")
    startup = function_body(source, "void idFileSystemLocal::Startup(")
    status = function_body(source, "pureStatus_t idFileSystemLocal::GetPackStatus(")
    update_game_pak_checksums = function_body(
        source,
        "bool idFileSystemLocal::UpdateGamePakChecksums(",
    )
    validate_download_pak = function_body(
        source,
        "int idFileSystemLocal::ValidateDownloadPakForChecksum(",
    )
    clear_pure_checksums = function_body(
        source,
        "void idFileSystemLocal::ClearPureChecksums(",
    )
    set_pure_server_checksums = function_body(
        source,
        "fsPureReply_t idFileSystemLocal::SetPureServerChecksums(",
    )
    get_pure_server_checksums = function_body(
        source,
        "void idFileSystemLocal::GetPureServerChecksums(",
    )
    find_dll = function_body(source, "void idFileSystemLocal::FindDLL(")
    get_mod_info = function_body(source, "bool idFileSystemLocal::GetModInfo(")
    send_pure_server_message = function_body(
        async_server,
        "bool idAsyncServer::SendPureServerMessage(",
    )
    send_reliable_pure_to_client = function_body(
        async_server,
        "bool idAsyncServer::SendReliablePureToClient(",
    )
    process_connect_message = function_body(
        async_server,
        "void idAsyncServer::ProcessConnectMessage(",
    )
    process_challenge_response = function_body(
        async_client,
        "void idAsyncClient::ProcessChallengeResponseMessage(",
    )

    require(source, '#include "openq4_paks_generated.h"', "filesystem generated pack checksum header")
    require(source, '#include "GameDirPolicy.h"', "portable game-directory policy integration")
    require(source, "IsOpenQ4PurePack", "filesystem pure-pack declaration")
    require(source, "ValidateOpenQ4Paks", "filesystem pack checksum declaration")
    table_lookup = function_body(source, "const openQ4PackTableEntry_t *idFileSystemLocal::FindOpenQ4PackTableEntry(")
    client_only_helper = function_body(source, "bool idFileSystemLocal::IsOpenQ4ClientOnlyPack(")
    require(source, "static const openQ4PackTableEntry_t openQ4PackTable[] = {\n\tOPENQ4_PACK_TABLE_ENTRIES\n};", "generated pack table drives the filesystem")
    require(helper, "FindOpenQ4PackTableEntry( pak )", "openQ4 pure-pack table lookup")
    require(helper, "!entry->clientOnly", "client-only packs stay out of the pure list")
    require(client_only_helper, "entry->clientOnly", "client-only pack classification")
    require(table_lookup, "IsGameDirPack( pak, OPENQ4_GAMEDIR )", "openQ4 pure-pack directory check")
    require(table_lookup, "FS_FindOpenQ4PackTableEntry( name.c_str() )", "openQ4 pack table filename check")
    reject(helper, '"pak1.pk4"', "pure-pack names come from the generated table")
    require(checksum_validator, "openQ4PackTable[ i ]", "every table pack is checked")
    require(checksum_validator, "expected.md5", "table digest is the expected checksum")
    require(checksum_validator, "MD5_FileChecksum", "openQ4 pack actual checksum")
    require(checksum_validator, "FindGamePackByName( expected.name, OPENQ4_GAMEDIR )", "openQ4 pack lookup")
    require(checksum_validator, "checksum mismatch for %s/%s", "openQ4 pack checksum diagnostic")
    require(checksum_validator, "#ifdef ID_DEDICATED", "dedicated servers treat client-only packs as optional")
    require(checksum_validator, "const bool optional = expected.clientOnly;", "dedicated optional client-only packs")
    require(checksum_validator, "onlyClientPacks = onlyClientPacks && expected.clientOnly;", "client-only failures can wait for localization")
    require(startup, "ValidateOpenQ4Paks( openQ4PakErrors, onlyClientPacks )", "startup pack checksum validation")
    require(startup, "openQ4 runtime content packs in '%s' are missing or modified", "startup pack checksum fatal")
    require_order(startup, "if ( !onlyClientPacks ) {", "openQ4 runtime content packs in '%s' are missing or modified", "core pack failures stay fatal at once")
    require(startup, "FS_RaiseDeferredContentPackFailure();", "a client-only pack failure after startup is reported at once")
    deferred = function_body(source, "void FS_RaiseDeferredContentPackFailure(")
    report = function_body(source, "static idStr FS_FormatOpenQ4PackProblems(")
    require(deferred, "fs_openQ4PackProblemsMayRaise = true;", "deferred pack report arms restarts")
    require(deferred, "common->FatalError( \"%s\", message.c_str() );", "deferred pack report is fatal")
    for key in ("#str_231700", "#str_231704", "#str_231705", "#str_231706", "#str_231707"):
        require(report, key, "localized content pack report")
    require(report, "problem.pack->contentsString", "localized pack contents")
    require(report, "PROJECT_RELEASES_URL", "pack report says where to get the pack")
    common_source = read("src/framework/Common.cpp")
    require_order(common_source, "InitLanguageDict( false, false );", "FS_RaiseDeferredContentPackFailure();", "client-only pack report after the language load")
    require(startup, "openQ4 runtime directory '%s' is missing a compatible mod.json", "startup mod manifest fatal")
    require(startup, "Retail Quake 4 media pk4 files must be installed in '%s', not '%s'.", "startup misplaced retail pk4 fatal")
    require(startup, "Put pak001.pk4 through pak022.pk4 in that folder", "startup retail pk4 location guidance")
    require(startup, "Do not put retail pk4 files in '%s'", "startup baseoq4 retail pk4 warning")
    require(misplaced_validator, "FindGamePackByName( info->name, OPENQ4_GAMEDIR )", "misplaced retail pk4 baseoq4 lookup")
    require(misplaced_validator, "with checksum 0x%08x but belongs in %s", "misplaced retail pk4 checksum diagnostic")
    require(misplaced_validator, "was found in both %s and %s; remove the copy from %s", "misplaced duplicate retail pk4 diagnostic")
    require(md5_header, "MD5_FileChecksum", "MD5 file checksum declaration")
    require(md5_source, "MD5_FileChecksum", "MD5 file checksum implementation")
    require(md5_source, "fopen( path, \"rb\" )", "MD5 file checksum binary read")
    require(
        md5_source,
        'static_assert( sizeof( unsigned int ) == 4, "MD5 requires 32-bit digest words" );',
        "fixed-width MD5 digest contract",
    )
    require(md5_source, "unsigned int\tdigest[4];", "fixed-width MD5 digest storage")
    require(md5_source, "unsigned int\tval;", "fixed-width MD5 checksum accumulator")
    require(md5_source, "val = digest[0] ^ digest[1] ^ digest[2] ^ digest[3];", "MD5 word folding")
    require(md5_source, "memset( ctx, 0, sizeof( *ctx ) );", "complete MD5 context clearing")
    reject(md5_source, "unsigned long\tdigest[4];", "LP64-unsafe MD5 digest storage")
    reject(md5_source, "memset( ctx, 0, sizeof( ctx ) );", "pointer-sized MD5 context clearing")

    require(file_system_header, "expectedSize;", "immutable URL download-size limit")
    require(curl_write, "nmemb > maximumSize / size", "libcurl callback multiplication guard")
    require(
        curl_write,
        "context->bytesWritten > maximumSize - byteCount",
        "libcurl callback cumulative-size guard",
    )
    require(
        curl_write,
        "byteCount > context->maximumBytes - context->bytesWritten",
        "libcurl callback advertised-size guard",
    )
    require(curl_write, '"download exceeds advertised size"', "oversized download diagnostic")
    require(
        curl_write,
        "fwrite( ptr, 1, byteCount",
        "libcurl byte-count write callback contract",
    )
    require(curl_write, "context->bytesWritten += written;", "download byte accounting")
    require(curl_write, "return written;", "libcurl exact write-result contract")
    require(curl_progress, "dltotal >= idMath::INT_MAX", "download-total progress narrowing guard")
    require(curl_progress, "dlnow >= idMath::INT_MAX", "download-current progress narrowing guard")
    require(curl_progress, "static_cast<int>( dltotal )", "checked download-total conversion")
    require(curl_progress, "static_cast<int>( dlnow )", "checked download-current conversion")
    require(
        curl_progress,
        "bgl->url.dltotal = bgl->url.expectedSize;",
        "package progress uses its advertised total",
    )
    require(
        curl_progress,
        "Min( reportedNow, bgl->url.expectedSize )",
        "package progress is capped at its advertised total",
    )
    reject(curl_progress, "bgl->url.dltotal = dltotal;", "unchecked download-total conversion")
    reject(curl_progress, "bgl->url.dlnow = dlnow;", "unchecked download-current conversion")
    require(
        download_progress_box,
        "static_cast<int64>( dlnow ) * ( progress_end - progress_start )",
        "download progress percentage overflow guard",
    )
    require(source, "~idScopedCurlEasySession()", "libcurl session lifetime guard")
    require(source, "curl_easy_cleanup( session );", "libcurl session cleanup")
    require_order(
        background_download,
        "idScopedCurlEasySession scopedSession( session );",
        "CURLOPT_ERRORBUFFER",
        "libcurl session guard before fallible options",
    )
    require(background_download, "error_buf[ 0 ] = '\\0';", "initialized libcurl error buffer")
    require(
        background_download,
        "bgl->url.expectedSize > 0 ? static_cast<size_t>( bgl->url.expectedSize ) : 0",
        "immutable download limit capture",
    )
    require(
        background_download,
        "curlDownloadContext_t downloadContext = { bgl, maximumBytes, 0 };",
        "per-transfer byte accounting context",
    )
    require(
        background_download,
        "CURLOPT_WRITEDATA, &downloadContext",
        "libcurl bounded write context ownership",
    )
    require_order(
        background_download,
        "curlDownloadContext_t downloadContext = { bgl, maximumBytes, 0 };",
        "bgl->url.dltotal = 0;",
        "download limit capture before mutable progress reset",
    )
    require(
        background_download,
        "downloadContext.bytesWritten != downloadContext.maximumBytes",
        "successful package transfer exact-size verification",
    )
    require(
        background_download,
        "bgl->url.dlstatus = CURLE_PARTIAL_FILE;",
        "short package transfer failure status",
    )
    require(
        background_download,
        "error_buf[ 0 ] != '\\0' ? error_buf : curl_easy_strerror( ret )",
        "libcurl fallback error diagnostic",
    )

    require(download_path_helper, "segmentStart[ 0 ] == ' '", "download path leading-space rejection")
    require(
        download_path_helper,
        "AsyncClient_IsWindowsDevicePathSegment( segmentStart, segmentLength )",
        "download path device-name rejection",
    )
    require(download_device_helper, 'idStr::Icmpn( segment, "con", 3 )', "CON device-name rejection")
    require(download_device_helper, 'idStr::Icmpn( segment, "com", 3 )', "COM device-name rejection")
    require(download_device_helper, "digit == 0xC2", "UTF-8 superscript device-name rejection")
    require(download_device_helper, "digit == 0xB9", "Windows-1252 superscript device-name rejection")
    require_order(
        download_file_cleanup,
        "idFile *file = download.f;",
        "download.f = NULL;",
        "background download file ownership transfer",
    )
    require_order(
        download_file_cleanup,
        "download.f = NULL;",
        "fileSystem->CloseFile( file );",
        "background download pointer detachment before file close",
    )
    require(download_file_cleanup, "if ( file != NULL )", "idempotent background download file cleanup")

    checksum_vectors = {
        b"": 0x3B75655E,
        b"abc": 0x275FA452,
        b"openQ4": 0x8A8BAE9B,
    }
    for payload, expected in checksum_vectors.items():
        digest_words = struct.unpack("<4I", hashlib.md5(payload).digest())
        actual = digest_words[0] ^ digest_words[1] ^ digest_words[2] ^ digest_words[3]
        if actual != expected:
            raise AssertionError(
                f"MD5 block-checksum semantics changed for {payload!r}: "
                f"expected 0x{expected:08x}, got 0x{actual:08x}"
            )

    require(async_client, "Client decl checksum: 0x%08x", "client decl checksum diagnostic")
    require(async_server, "Server decl checksum: 0x%08x", "server decl checksum diagnostic")
    require(async_server, "client=0x%08x server=0x%08x (non-pure)", "non-pure mismatch diagnostic")
    require(async_server, "client=0x%08x server=0x%08x (pure)", "pure mismatch diagnostic")
    reject(async_server, 'serverInfo.SetInt( "si_pure", 0 )', "forced pure-server disable")
    reject(async_server, "forcing si_pure 0", "forced pure-server disable diagnostic")
    require(
        async_network_header,
        "const int ASYNC_PROTOCOL_MINOR\t\t= 41;",
        "Quake 4 1.4.2 protocol compatibility",
    )
    require(
        source,
        "static const int OPENQ4_Q4_142_GAME300_PAK_CHECKSUM = 0x68fb90b1;",
        "platform-independent pure game-module compatibility token",
    )
    require(file_system_header, "MAX_GAME_OS\t\t\t\t\t= 6;", "pure OS table capacity")

    for origin in (
        "GAME_MODULE_ORIGIN_NONE",
        "GAME_MODULE_ORIGIN_ACTIVE_MOD",
        "GAME_MODULE_ORIGIN_BASE_GAME",
        "GAME_MODULE_ORIGIN_PACKAGE_ROOT",
    ):
        require(source, origin, "trusted game-module origin model")
    require(find_dll, "gameModuleOrigin_t resolvedOrigin = GAME_MODULE_ORIGIN_NONE;", "module-origin reset")
    require(find_dll, "GAME_MODULE_ORIGIN_ACTIVE_MOD", "active-mod module origin")
    require(find_dll, "GAME_MODULE_ORIGIN_BASE_GAME", "base-game module origin")
    require(find_dll, "GAME_MODULE_ORIGIN_PACKAGE_ROOT", "flat package-root module origin")
    require(find_dll, "gameModuleOrigin = resolvedOrigin;", "resolved module-origin persistence")
    require(
        find_dll,
        "gamePakChecksum = dllFile ? OPENQ4_Q4_142_GAME300_PAK_CHECKSUM : 0;",
        "compatibility token only for a resolved trusted module",
    )

    for token in (
        "MAX_SEGMENT_BYTES = 255",
        "IsWindowsDeviceName(",
        "IsPortableSegment(",
        "*scan == '/'",
        "*scan == '\\\\'",
        "*scan == ':'",
        "segment[ 0 ] == '.'",
        "segment[ 1 ] == '.'",
    ):
        require(game_dir_policy, token, "portable single-segment game-directory policy")
    require(
        get_mod_info,
        "!idGameDirPolicy::IsPortableSegment( modDir )",
        "mod manifest directory validation",
    )
    require_order(
        get_mod_info,
        "!idGameDirPolicy::IsPortableSegment( modDir )",
        "const char *search[ 3 ]",
        "mod directory validation before manifest roots",
    )
    require(
        get_mod_info,
        "mod directory must be one portable directory segment",
        "actionable unsafe mod-directory failure",
    )
    require(
        find_dll,
        "!idGameDirPolicy::IsPortableSegment( moduleGameDir )",
        "trusted module game-directory validation",
    )
    require_order(
        find_dll,
        "!idGameDirPolicy::IsPortableSegment( moduleGameDir )",
        "FS_AppendGameModuleSearchPath(",
        "game-directory validation before trusted module path construction",
    )
    for token in (
        "gameDLLChecksum = 0;",
        "gamePakChecksum = 0;",
        "gameModuleOrigin = GAME_MODULE_ORIGIN_NONE;",
        "_dllPath[ 0 ] = '\\0';",
        "return;",
    ):
        require(find_dll, token, "unsafe fs_game module lookup fail-closed state")
    require(process_challenge_response, "fileSystem->GetModInfo( serverGameBase", "server base-mod validation")
    require(process_challenge_response, "fileSystem->GetModInfo( serverGame", "server mod validation")
    require_order(
        process_challenge_response,
        "fileSystem->GetModInfo( serverGame",
        'cvarSystem->SetCVarString( "fs_game", serverGame );',
        "server mod validation before fs_game mutation",
    )

    require(update_game_pak_checksums, "gameDLLChecksum == 0", "missing local game-module rejection")
    require(
        update_game_pak_checksums,
        "gamePakChecksum != OPENQ4_Q4_142_GAME300_PAK_CHECKSUM",
        "unexpected local compatibility-token rejection",
    )
    require(
        update_game_pak_checksums,
        "gameModuleOrigin == GAME_MODULE_ORIGIN_NONE",
        "untrusted module-origin rejection",
    )
    require(
        update_game_pak_checksums,
        "gameModuleOrigin == GAME_MODULE_ORIGIN_ACTIVE_MOD",
        "active-mod module-origin classification",
    )
    require(
        update_game_pak_checksums,
        '!cvarSystem->GetCVarBool( "net_serverAllowServerMod" )',
        "explicit server-mod opt-in gate",
    )
    require(
        update_game_pak_checksums,
        "for ( int os = 0; os <= 2; ++os )",
        "legacy Windows/Linux/macOS compatibility range",
    )
    require_order(
        update_game_pak_checksums,
        "memset( gamePakForOS, 0, sizeof( gamePakForOS ) );",
        "for ( int os = 0; os <= 2; ++os )",
        "unsupported pure OS entries cleared before legacy OS mapping",
    )
    require(
        update_game_pak_checksums,
        "gamePakForOS[ os ] = OPENQ4_Q4_142_GAME300_PAK_CHECKSUM;",
        "platform-independent compatibility-token mapping",
    )
    for obsolete_parser_symbol in (
        "BINARY_CONFIG",
        '"binary.conf"',
        "HashFileName(",
        "ReadFile(",
        "idLexer",
        "ParseInt(",
        "atoi(",
    ):
        reject(
            update_game_pak_checksums,
            obsolete_parser_symbol,
            "pure game-module mapping without retail binary.conf parsing",
        )

    require(validate_download_pak, "pak->binary == BINARY_UNKNOWN", "lazy package binary classification")
    require(validate_download_pak, "HashFileName( BINARY_CONFIG )", "binary.conf presence lookup")
    require(validate_download_pak, "pak->binary = BINARY_NO", "content-only package classification")
    require(validate_download_pak, "pak->binary = BINARY_YES", "binary-marked package classification")
    reject(validate_download_pak, "ReadFile(", "package binary marker presence-only classification")
    require(clear_pure_checksums, "memset( gamePakForOS, 0, sizeof( gamePakForOS ) );", "non-pure OS-mask reset")

    require(
        set_pure_server_checksums,
        "*missingGamePakChecksum = 0;",
        "code-pak download output stays empty",
    )
    if set_pure_server_checksums.count("*missingGamePakChecksum =") != 1:
        raise AssertionError("Pure negotiation must never request a downloadable game-code pak")
    require(
        set_pure_server_checksums,
        "_gamePakChecksum != OPENQ4_Q4_142_GAME300_PAK_CHECKSUM",
        "server compatibility-token validation",
    )
    require(
        set_pure_server_checksums,
        "gamePakChecksum != OPENQ4_Q4_142_GAME300_PAK_CHECKSUM",
        "local compatibility-token validation",
    )
    require(set_pure_server_checksums, "gameDLLChecksum == 0", "missing local module rejection")
    require(
        set_pure_server_checksums,
        "gameModuleOrigin == GAME_MODULE_ORIGIN_NONE",
        "untrusted local module rejection",
    )
    require(set_pure_server_checksums, "return PURE_NODLL;", "unsupported token fail-closed result")
    require_order(
        set_pure_server_checksums,
        "_gamePakChecksum != OPENQ4_Q4_142_GAME300_PAK_CHECKSUM",
        "if ( pureChecksums[ 0 ] == 0 )",
        "game-module token validation before asset-list processing",
    )
    for forbidden_code_pak_path in (
        "GetPackForChecksum( _gamePakChecksum",
        "GetPackForChecksum( gamePakChecksum",
        "*missingGamePakChecksum = _gamePakChecksum",
        "*missingGamePakChecksum = gamePakChecksum",
        "gamePakChecksum = _gamePakChecksum",
        "restartGamePakChecksum",
        "BINARY_CONFIG",
        '"binary.conf"',
    ):
        reject(
            set_pure_server_checksums,
            forbidden_code_pak_path,
            "compatibility token cannot select, download, or restart into game code",
        )

    require(get_pure_server_checksums, "OS >= 0 && OS < MAX_GAME_OS", "pure checksum OS bounds")
    require(get_pure_server_checksums, "else if ( OS == -1 )", "local pure-token sentinel")
    require(get_pure_server_checksums, "*_gamePakChecksum = 0;", "invalid pure OS fail-closed token")
    require_order(
        get_pure_server_checksums,
        "OS >= 0 && OS < MAX_GAME_OS",
        "OS == -1",
        "bounded remote OS lookup before local-only sentinel",
    )

    require(process_connect_message, "if ( OS < 0 || OS >= MAX_GAME_OS )", "pure client OS bounds")
    require(
        process_connect_message,
        "const int osMask = fileSystem->GetOSMask();",
        "pure supported-OS mask lookup",
    )
    require(
        process_connect_message,
        "static_cast<unsigned int>( osMask ) & ( 1u << OS )",
        "defined unsigned pure OS-mask shift",
    )
    require_order(
        process_connect_message,
        "if ( OS < 0 || OS >= MAX_GAME_OS )",
        "const int osMask = fileSystem->GetOSMask();",
        "OS range validation before OS-mask lookup and shift",
    )
    require_order(
        process_connect_message,
        "const int osMask = fileSystem->GetOSMask();",
        "ValidateChallenge( from, challenge, clientId )",
        "pure supported-OS rejection before challenge state use",
    )
    require_order(
        process_connect_message,
        "static_cast<unsigned int>( osMask ) & ( 1u << OS )",
        "ValidateChallenge( from, challenge, clientId )",
        "pure supported-OS bit test before challenge state use",
    )
    require_order(
        process_connect_message,
        "if ( OS < 0 || OS >= MAX_GAME_OS )",
        "challenges[ ichallenge ].OS = OS;",
        "OS range validation before challenge-state storage",
    )

    for send_body, message_init, context in (
        (send_pure_server_message, "outMsg.Init(", "connectionless pure send"),
        (send_reliable_pure_to_client, "msg.Init(", "reliable pure send"),
    ):
        require(
            send_body,
            "if ( !serverChecksums[ 0 ] || !gamePakChecksum )",
            f"{context} fail-closed prerequisites",
        )
        require_order(
            send_body,
            "if ( !serverChecksums[ 0 ] || !gamePakChecksum )",
            message_init,
            f"{context} prerequisites before packet construction",
        )
        guard_start = send_body.find("if ( !serverChecksums[ 0 ] || !gamePakChecksum )")
        guard_end = send_body.find(message_init, guard_start)
        require(send_body[guard_start:guard_end], "return false;", f"{context} fail-closed return")
    require(
        download_request,
        "dlSize[ MAX_PURE_PAKS ] = {};",
        "download response size initialization for the no-game-pak slot",
    )
    require(
        download_request,
        "totalDlSize > idMath::INT_MAX - dlSize[ i ]",
        "download response total-size overflow guard",
    )
    require(
        download_request,
        "pakURLs.Num() > pakNames.Num()",
        "download response URL-count bounds guard",
    )
    require_order(
        download_request,
        "pakURLs.Num() > pakNames.Num()",
        "for ( i = 0; i < pakURLs.Num(); i++ )",
        "download response URL-count validation before indexed access",
    )
    require(download_path_helper, "path[ 0 ] == '/'", "absolute server download path rejection")
    require(download_path_helper, "path[ 0 ] == '\\\\'", "rooted backslash server download path rejection")
    require(download_path_helper, "c == '\\\\' || c == ':'", "backslash and drive-path rejection")
    require(download_path_helper, "static_cast<unsigned char>( c ) < 32", "control-character path rejection")
    require(download_path_helper, "c == '<'", "Windows-reserved punctuation rejection")
    require(download_path_helper, "c == '*'", "Windows-reserved punctuation rejection")
    require(download_path_helper, "segmentLength == 0", "empty download path segment rejection")
    require(download_path_helper, "segmentStart[ 0 ] == '.'", "dot download path segment rejection")
    require(download_path_helper, "segmentStart[ 1 ] == '.'", "parent download path segment rejection")
    require(
        download_path_helper,
        "segmentStart[ segmentLength - 1 ] == '.'",
        "Windows trailing-dot path rejection",
    )
    require(
        download_path_helper,
        "segmentStart[ segmentLength - 1 ] == ' '",
        "Windows trailing-space path rejection",
    )
    require(download_path_helper, "segmentLength > 4", "download package filename requirement")
    require(
        download_path_helper,
        'idStr::Icmp( scan - 4, ".pk4" ) == 0',
        "case-insensitive PK4 download extension requirement",
    )

    download_yes = download_info[
        download_info.find("if ( pakDl == SERVER_PAK_YES )") :
        download_info.find("} else if ( pakDl == SERVER_PAK_NO )")
    ]
    require(download_yes, "AsyncClient_IsSafeDownloadPath( buf )", "server download path validation")
    require_order(
        download_yes,
        "msg.ReadString( buf, MAX_STRING_CHARS );",
        "AsyncClient_IsSafeDownloadPath( buf )",
        "server download path validation after decode",
    )
    require_order(
        download_yes,
        "AsyncClient_IsSafeDownloadPath( buf )",
        "entry.filename = buf;",
        "server download path validation before storage",
    )
    require(download_yes, "entry.size <= 0", "non-positive server download-size rejection")
    require(
        download_yes,
        "totalDlSize > idMath::INT_MAX - entry.size",
        "server download total-size overflow guard",
    )
    require_order(
        download_yes,
        "totalDlSize > idMath::INT_MAX - entry.size",
        "totalDlSize += entry.size;",
        "server download size validation before accumulation",
    )
    invalid_download_type = download_info[
        download_info.find("} else if ( pakDl != SERVER_PAK_END )") :
        download_info.find("} while ( pakDl != SERVER_PAK_END )")
    ]
    require(
        invalid_download_type,
        "server sent invalid download entry type",
        "invalid download discriminator diagnostic",
    )
    require(invalid_download_type, "dlList.Clear();", "invalid download discriminator cleanup")
    require(invalid_download_type, "return;", "invalid download discriminator fail-closed return")
    reject(download_info, "assert( pakDl == SERVER_PAK_END );", "release-only download discriminator validation")

    require(
        licensee_header,
        '#define PROJECT_RELEASES_URL\t\t\tPROJECT_REPO "/releases"',
        "compile-time openQ4 releases destination",
    )
    require(
        licensee_header,
        '#define PROJECT_REPO\t\t\t\t\t"https://github.com/themuffinator/openQ4"',
        "HTTPS openQ4 project destination",
    )
    require(
        version_message,
        'updateMSG = common->GetLanguageDict()->GetString( "#str_104330" );',
        "fixed localized update notification",
    )
    require(
        version_message,
        "ignoredNetworkField",
        "legacy update fields are consumed without storage or display",
    )
    if version_message.count("msg.ReadString(") != 3 or version_message.count("msg.ReadByte()") != 2:
        raise AssertionError("Legacy update reply fields must remain wire-compatible and inert")
    reject(version_message, "updateMSG = ignoredNetworkField", "network-controlled update instructions")
    require(version_check, 'msg.WriteString( "" );', "empty legacy update identity field")
    reject(version_check, 'GetCVarString( "com_guid" )', "update-check persistent GUID disclosure")
    update_prompt = download_handler[
        download_handler.find("if ( updateState == UPDATE_READY )") :
        download_handler.find("} else if ( dlList.Num() )")
    ]
    require(update_prompt, "sys->OpenURL( PROJECT_RELEASES_URL, false );", "fixed update information page")
    require(update_prompt, "updateState = UPDATE_DONE;", "update notification terminal state")
    require(update_prompt, "showUpdateMessage = false;", "update notification prompt cleanup")
    for forbidden in (
        "BackgroundDownload",
        "OpenFileWrite",
        "StartProcess",
        "DLTYPE_URL",
        "file://",
        "expectedSize",
        "RemoveFile",
    ):
        reject(update_prompt, forbidden, "notification-only updater")
    for removed_member in (
        "UPDATE_DLING",
        "updateDirectDownload",
        "updateURL",
        "updateFile",
        "updateMime",
        "updateFallback",
        "SendVersionDLUpdate",
    ):
        reject(async_client_header, removed_member, "legacy executable updater state")
        reject(async_client, removed_member, "legacy executable updater implementation")
    reject(file_system_header, "FILE_EXEC", "legacy executable updater MIME action")
    reject(file_system_header, "dlMime_t", "legacy updater MIME type")

    package_setup = download_handler[
        download_handler.find("fileSystem->MakeTemporaryFile( )") :
        download_handler.find("session->DownloadProgressBox( &backgroundDownload, dltitle")
    ]
    require(
        package_setup,
        "backgroundDownload.url.expectedSize = dlList[ 0 ].size;",
        "package advertised-size limit",
    )
    require(package_setup, "backgroundDownload.url.dltotal = 0;", "progress total initialization")
    reject(
        package_setup,
        "backgroundDownload.url.dltotal = dlList[ 0 ].size;",
        "advertised-size limit stored in mutable progress",
    )
    require_order(
        package_setup,
        "backgroundDownload.url.expectedSize = dlList[ 0 ].size;",
        "fileSystem->BackgroundDownload( &backgroundDownload );",
        "package size limit set before worker handoff",
    )

    if bounded_download_write_model(0, 2, 5, 10) != 10:
        raise AssertionError("Bounded download model rejected an exact-size write")
    if bounded_download_write_model(9, 1, 2, 10) is not None:
        raise AssertionError("Bounded download model accepted a body above its advertised size")
    if bounded_download_write_model(0, 2, SIZE_T_MAX // 2 + 1, 0) is not None:
        raise AssertionError("Bounded download model accepted overflowing callback dimensions")
    if bounded_download_write_model(SIZE_T_MAX, 1, 1, 0) is not None:
        raise AssertionError("Bounded download model accepted overflowing cumulative bytes")
    if bounded_download_write_model(9, 1, 2, 0) != 11:
        raise AssertionError("Unbounded transfer model unexpectedly applied a package-size limit")

    require_order(
        download_handler,
        "existingDestination = fileSystem->OpenExplicitFileRead( finalPath );",
        "fileSystem->CreateOSPath( finalPath );",
        "existing download destination check before directory creation",
    )
    existing_destination_failure = download_handler[
        download_handler.find("if ( existingDestination )") :
        download_handler.find("fileSystem->CreateOSPath( finalPath );")
    ]
    require(
        existing_destination_failure,
        "fileSystem->CloseFile( existingDestination );",
        "existing download destination probe cleanup",
    )
    require(
        existing_destination_failure,
        "refusing to replace existing download destination",
        "existing download destination diagnostic",
    )
    require(
        existing_destination_failure,
        "AsyncClient_CloseBackgroundDownloadFile( backgroundDownload );",
        "existing destination temporary-file cleanup",
    )
    require(existing_destination_failure, "dlList.Clear();", "existing destination download-list cleanup")

    require_order(
        download_handler,
        "saveas = fileSystem->OpenExplicitFileWrite( finalPath );",
        "if ( !saveas )",
        "download destination open failure handling",
    )
    require_order(
        download_handler,
        "if ( !saveas )",
        "saveas->Write( buf, readlen );",
        "download destination null guard",
    )
    destination_failure = download_handler[
        download_handler.find("if ( !saveas )") :
        download_handler.find("buf = (byte*)Mem_Alloc( CHUNK_SIZE );")
    ]
    require(
        destination_failure,
        "AsyncClient_CloseBackgroundDownloadFile( backgroundDownload );",
        "failed destination temporary-file cleanup",
    )
    require(destination_failure, "dlList.Clear();", "failed download-list cleanup")
    require(destination_failure, '"#str_07215"', "localized download failure message")
    require(
        download_handler,
        "fileSystem->RemoveExplicitFile( finalPath );",
        "checksum failure removes only the verified download destination",
    )

    successful_copy_cleanup = download_handler[
        download_handler.find("while ( remainlen )") :
        download_handler.find("fileSystem->CloseFile( saveas );")
    ]
    require(
        successful_copy_cleanup,
        "AsyncClient_CloseBackgroundDownloadFile( backgroundDownload );",
        "successful package download temporary-file cleanup",
    )

    failed_download_cleanup = download_handler[
        download_handler.find('common->Warning( "download failed:') :
        download_handler.find("pakCount++;")
    ]
    require(
        failed_download_cleanup,
        "AsyncClient_CloseBackgroundDownloadFile( backgroundDownload );",
        "failed or cancelled package download temporary-file cleanup",
    )
    require_order(
        failed_download_cleanup,
        "AsyncClient_CloseBackgroundDownloadFile( backgroundDownload );",
        "dlList.Clear();",
        "failed package download file cleanup before return",
    )
    reject(download_handler, "fileSystem->CloseFile( f );", "direct background download file close")
    reject(
        download_handler,
        "fileSystem->CloseFile( backgroundDownload.f );",
        "direct background download member close",
    )
    cleanup_call = "AsyncClient_CloseBackgroundDownloadFile( backgroundDownload );"
    if download_handler.count(cleanup_call) != 4:
        raise AssertionError("Every background download completion path must release its file exactly once")

    valid_download_paths = (
        "baseoq4/example.pk4",
        "baseoq4/downloads/example.pk4",
        "baseoq4/downloads/example.PK4",
        "mods/example.pk4",
        "example.pk4",
    )
    invalid_download_paths = (
        "",
        "/baseoq4/example.pk4",
        "\\baseoq4\\example.pk4",
        "C:/baseoq4/example.pk4",
        "baseoq4\\example.pk4",
        "baseoq4/mixed\\example.pk4",
        ".",
        "..",
        "./example.pk4",
        "../example.pk4",
        "baseoq4/./example.pk4",
        "baseoq4/../example.pk4",
        "baseoq4/.. /example.pk4",
        "baseoq4/trailing./example.pk4",
        "baseoq4/trailing /example.pk4",
        "baseoq4//example.pk4",
        "baseoq4/example.pk4/",
        "baseoq4/example.pk4.",
        "baseoq4/example.pk4 ",
        " baseoq4/example.pk4",
        "baseoq4/ downloads/example.pk4",
        "baseoq4/example:alternate.pk4",
        "baseoq4/example?.pk4",
        "baseoq4/example*.pk4",
        "baseoq4/example|alternate.pk4",
        "baseoq4/example\x01.pk4",
        "baseoq4/example.zip",
        "baseoq4/.pk4",
        "CON.pk4",
        "baseoq4/prn/example.pk4",
        "baseoq4/AUX.txt/example.pk4",
        "baseoq4/nul/example.pk4",
        "baseoq4/COM1.pk4",
        "baseoq4/lpt9/example.pk4",
        "baseoq4/COM¹.pk4",
        "baseoq4/LPT²/example.pk4",
        "baseoq4/com³.txt/example.pk4",
    )
    for path in valid_download_paths:
        if not is_safe_download_path_model(path):
            raise AssertionError(f"Valid server download path rejected by model: {path!r}")
    for path in invalid_download_paths:
        if is_safe_download_path_model(path):
            raise AssertionError(f"Unsafe server download path accepted by model: {path!r}")

    require(status, "IsOpenQ4PurePack( pak )", "GetPackStatus openQ4 pure-pack check")
    require_order(
        status,
        "IsOpenQ4PurePack( pak )",
        "FindOfficialPk4Info",
        "GetPackStatus openQ4 pure-pack precedence",
    )
    require_order(
        status,
        "IsOpenQ4PurePack( pak )",
        "// check content for PURE_NEVER",
        "GetPackStatus openQ4 pure-pack precedence",
    )

    openq4_check = status[
        status.find("if ( IsOpenQ4PurePack( pak ) )") :
        status.find("// Keep the stock Quake 4 base media")
    ]
    require(openq4_check, "pak->pureStatus = PURE_ALWAYS;", "openQ4 pure-pack status")
    require(openq4_check, "return PURE_ALWAYS;", "openQ4 pure-pack return")
    require(openq4_check, "if ( IsOpenQ4ClientOnlyPack( pak ) ) {\n\t\tpak->pureStatus = PURE_NEVER;\n\t\treturn PURE_NEVER;", "client-only packs never enter a pure list")


def validate_install_error_console_contract() -> None:
    posix_main = read("src/sys/posix/posix_main.cpp")
    posix_signal = read("src/sys/posix/posix_signal.cpp")
    posix_console = read("src/sys/posix/posix_syscon.cpp")
    posix_header = read("src/sys/posix/posix_public.h")
    win_console = read("src/sys/win32/win_syscon.cpp")
    console_theme = read("src/sys/sys_console_theme.h")

    require(posix_header, "Posix_ConsoleSetFatalError", "POSIX fatal console declaration")
    require(posix_header, "Posix_ConsoleFatalErrorWait", "POSIX fatal console declaration")
    require(posix_main, "Sys_SetFatalError( text );", "POSIX Sys_Error fatal banner update")
    require(posix_main, "Sys_Printf( \"Sys_Error: %s\\n\", text );", "POSIX Sys_Error console log text")
    require(posix_main, "Posix_ConsoleFatalErrorWait();", "POSIX Sys_Error visible console wait")
    require(posix_signal, "Posix_ConsoleSetFatalError( fatalError );", "POSIX fatal signal bridge")
    require(posix_console, "POSIX_CONSOLE_STATUS_HEIGHT", "POSIX console status strip")
    require(posix_console, "Posix_ConsoleDrawStatus", "POSIX console status rendering")
    reject(posix_console, "statusText = \"ERROR: \";", "POSIX console fatal status text")
    require(posix_console, "statusText.Append( scan, 1 );", "POSIX console fatal status text")
    # Presentation moved to the shared cross-platform theme; see
    # tools/tests/system_console_presentation.py for the full contract.
    require(console_theme, '#define SYSCON_STATUS_READY_TEXT\t"System console ready"', "shared console ready status text")
    require(posix_console, "statusText = SYSCON_STATUS_READY_TEXT;", "POSIX console ready status")
    require(posix_console, "s_consoleWindow.forceFatalWindow = true;", "POSIX fatal console forced visibility")
    require(
        posix_console,
        "if ( !s_consoleWindow.forceFatalWindow &&\n\t\t ( cvarSystem == NULL || !cvarSystem->IsInitialized() ||\n\t\t   ( !sys_consoleWindow.GetBool() && !s_consoleWindow.quitOnClose ) ) )",
        "POSIX fatal console avoids released cvar storage",
    )
    require(posix_console, "s_consoleWindow.exitRequested = true;", "POSIX fatal console quit/close exit")
    require(win_console, "SYSCON_STATUS_READY_TEXT", "Windows console ready status")
    require(win_console, "SYSCON_WIN_RGB(PANEL)", "Windows console status background color")
    require(win_console, "SYSCON_WIN_RGB(TEXT)", "Windows console status text color")
    require(win_console, "SYSCON_WIN_RGB(ALERT_PANEL)", "Windows console fatal status background color")
    require(win_console, "SetWindowText(s_wcd.hwndErrorBox, s_wcd.errorString);", "Windows console status text update")


def validate_packager_pure_pack_contract() -> None:
    package = load_package_module()
    pak_helper = read("tools/build/openq4_pak.py")
    packager = read("tools/build/package_nightly.py")
    work = ROOT / ".tmp" / "openq4-pure-pack-contract"

    require(pak_helper, "DETERMINISTIC_ZIP_TIMESTAMP", "deterministic zip metadata")
    require(pak_helper, "PAK1_NAME", "pak1 helper name")
    require(pak_helper, "OPENQ4_REQUIRED_PK4_FILES_BY_PACK", "per-pack required files")
    require(pak_helper, "OPENQ4_PK4_FORBIDDEN_FILES", "package pure-pack marker guard")
    require(pak_helper, '"binary.conf"', "package pure-pack binary marker guard")
    require(pak_helper, '"addon.conf"', "package pure-pack addon marker guard")
    require(pak_helper, "must remain a pure runtime pack", "package pure-pack marker diagnostic")
    require(pak_helper, "hashlib.md5", "pack full-file checksum")
    require(pak_helper, "copy_game_pk4", "package staged pack copy helper")
    require(packager, "from openq4_pak import", "release packager shared pak helper")
    require(packager, "OPENQ4_PACK_NAMES", "release packager pack list")
    require(packager, "for pak_name in OPENQ4_PACK_NAMES", "release packager iterates openQ4 packs")
    require(packager, "staged_game_pk4_path", "release packager staged pack preference")
    require(packager, "copy_game_pk4(staged_game_pk4_path, game_pk4_path, pak_name=pak_name)", "release packager staged pack copy")
    require(packager, "md5 {pk4_result.md5_hex}", "release packager checksum summary")

    shutil.rmtree(work, ignore_errors=True)
    try:
        for pak_name in ("pak0.pk4", "pak1.pk4", "pak2.pk4"):
            for marker in ("binary.conf", "addon.conf"):
                shutil.rmtree(work, ignore_errors=True)
                install_game_dir = work / "baseoq4"
                destination_pk4 = work / pak_name
                write_test_file(install_game_dir / marker, b"marker\n")
                expect_runtime_error(
                    "must remain a pure runtime pack",
                    lambda: package.create_game_pk4(install_game_dir, destination_pk4, pak_name),
                    f"{pak_name} marker rejection for {marker}",
                )
    finally:
        shutil.rmtree(work, ignore_errors=True)


def validate_build_pak0_contract() -> None:
    meson = read("meson.build")
    baseoq4_meson = read("content/baseoq4/meson.build")
    build_pak0 = read("tools/build/build_pak0.py")
    build_openq4_pack = read("tools/build/build_openq4_pack.py")
    generate_pak_header = read("tools/build/generate_pak_header.py")
    write_pak_manifest = read("tools/build/write_pak_manifest.py")
    pak_helper = read("tools/build/openq4_pak.py")
    install_guard = read("tools/build/check_staged_content_edits.py")
    windows_runtime = read("tools/build/windows_runtime.py")
    validator = read("tools/validation/openq4_validate.py")
    work = ROOT / ".tmp" / "openq4-pak0-build-contract"

    require(meson, "custom_target(", "Meson pack build target")
    require(meson, "'content_packs.py',\n    'meson-list',", "Meson pack targets come from the content pack table")
    require(meson, "foreach openq4_pack_row : openq4_content_pack_table", "Meson builds every table pack")
    require(meson, "'openq4_' + openq4_pack_stem,", "Meson openQ4 pack build target name")
    require(meson, "'openq4_paks_generated_header'", "Meson generated pack checksum target name")
    require(meson, "'openq4_' + openq4_pack_stem + '_source_manifest'", "Meson pack source manifest target")
    require(meson, "write_pak_manifest.py", "Meson pack source manifest refresh")
    require(meson, "build_openq4_pack.py", "Meson single-pack build script")
    require(meson, "generate_pak_header.py", "Meson pack header generation script")
    require(meson, "'openq4_paks_generated.h'", "Meson generated pack checksum header")
    require(meson, "input: openq4_pack_manifest", "Meson pack depends on its source manifest")
    require(meson, "input: openq4_pak_files", "Meson pack header reads every built pack")
    require(meson, "--manifest", "Meson pack target consumes source manifest")
    require(meson, "build_by_default: true", "Meson pack default build")
    require(meson, "build_always_stale: true", "Meson source manifests rescan pack directories")
    reject(meson, "openq4_pak0_source_paths", "Meson pak0 must not bake a configured source-file list")
    reject(meson, "openq4_pak1_source_paths", "Meson pak1 must not bake a configured source-file list")
    require(meson, "install: true", "Meson pack install")
    require(meson, "install: false", "Meson does not install generated header")
    require(meson, "install_dir: install_game_dir", "Meson installs generated packs")
    engine_generated_headers = "openq4_engine_sources += [openq4_paks_generated_header, generated_version_header]"
    require(meson, engine_generated_headers, "engine depends on pack checksum and version headers")
    require_order(meson, "'openq4_' + openq4_pack_stem,", "'openq4_paks_generated_header'", "packs before checksum header")
    require_order(meson, "'openq4_paks_generated_header'", engine_generated_headers, "checksum header before engine sources")

    require(baseoq4_meson, "baseoq4_manifest", "loose mod.json install")
    reject(baseoq4_meson, "install_subdir(", "baseoq4 content should be inside openQ4 PK4s")
    reject(baseoq4_meson, "'openq4_defaults.cfg'", "baseoq4 loose config install")
    reject(baseoq4_meson, "'default.cfg'", "baseoq4 loose default config install")

    require(pak_helper, 'f"#define {stem}_MD5', "generated per-pack checksum macro")
    require(pak_helper, "OPENQ4_PACK_TABLE_ENTRIES", "generated pack table macro")
    require(pak_helper, "load_content_pack_specs", "pack helper reads the content pack table")
    require(build_pak0, "format_openq4_paks_header", "standalone pack builder uses shared header formatter")
    require(build_pak0, "create_game_pk4", "build pak0 helper")
    require(build_pak0, "OPENQ4_CONTENT_PACKS", "standalone pack builder builds every table pack")
    require(build_pak0, "--stage-dir", "builddir direct-run pack staging")
    require(build_openq4_pack, "--pak-name", "single-pack build script pack selection")
    require(build_openq4_pack, "create_game_pk4", "single-pack build script helper")
    require(build_openq4_pack, "--manifest", "single-pack build script manifest dependency")
    require(build_openq4_pack, "--stage-out", "single-pack builddir direct-run staging")
    require(generate_pak_header, "format_openq4_paks_header", "generated pack header shared formatter")
    require(generate_pak_header, "inspect_game_pk4", "generated pack header validates built packs")
    require(pak_helper, "format_pk4_source_manifest", "pack source manifest matches packer filtering")
    require(write_pak_manifest, "format_pk4_source_manifest", "Meson pack manifest source list matches packer filtering")
    require(write_pak_manifest, "write_text_if_changed", "Meson pack manifest avoids unnecessary downstream rebuilds")
    require(install_guard, "STALE_LOOSE_ROOT_FILES", "install guard stale loose root files")
    require(install_guard, "STALE_LOOSE_SUBDIRS", "install guard stale loose content dirs")
    require(install_guard, '"default.cfg"', "install guard stale loose default config")
    require(install_guard, '"env"', "install guard stale loose env content")
    require(install_guard, '"scripts"', "install guard stale loose map scripts")
    require(install_guard, "assert_inside", "install guard deletion containment")
    require(install_guard, "shutil.rmtree", "install guard stale loose directory pruning")
    require(windows_runtime, "build_game_pk4s", "Windows direct-run pack staging")
    require(windows_runtime, "OPENQ4_PACK_NAMES", "Windows direct-run pack destinations")
    require(validator, "content_packs.json", "staged payload requires every table pack")
    require(validator, "STAGED_FORBIDDEN_LOOSE_GAME_PATHS", "staged payload forbids stale loose content")
    require(validator, '"openq4_defaults.cfg"', "staged payload forbids loose defaults")
    require(validator, "must live inside openQ4 PK4s", "staged payload stale loose diagnostic")

    shutil.rmtree(work, ignore_errors=True)
    try:
        sys.path.insert(0, str(ROOT / "tools" / "build"))
        from openq4_pak import OPENQ4_CONTENT_PACKS  # noqa: E402

        source_root = work / "source"
        for spec in OPENQ4_CONTENT_PACKS:
            files = sorted(spec.required_files) or ["placeholder.txt"]
            for relative_path in files:
                write_test_file(source_root / spec.source / relative_path, f"{relative_path}\n".encode("utf-8"))

        out_dir = work / "out"
        stage_dir = work / "stage" / "baseoq4"
        header_out = work / "openq4_paks_generated.h"
        subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools" / "build" / "build_pak0.py"),
                "--source-root",
                str(source_root),
                "--out-dir",
                str(out_dir),
                "--header-out",
                str(header_out),
                "--stage-dir",
                str(stage_dir),
            ],
            cwd=ROOT,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        header = header_out.read_text(encoding="utf-8")
        require(header, f"#define OPENQ4_PACK_COUNT {len(OPENQ4_CONTENT_PACKS)}", "generated pack count")
        for spec in OPENQ4_CONTENT_PACKS:
            built = out_dir / spec.name
            stem = spec.macro_stem
            digest = hashlib.md5(built.read_bytes()).hexdigest()
            require(header, f'#define {stem}_MD5 "{digest}"', f"generated {spec.name} checksum header")
            require(header, f"#define {stem}_FILE_COUNT {max(1, len(spec.required_files))}", f"generated {spec.name} file count")
            require(header, f"#define {stem}_CLIENT_ONLY {int(spec.client_only)}", f"generated {spec.name} audience")
            require(header, f'{{ "{spec.name}", {stem}_MD5,', f"generated {spec.name} table entry")
            if built.read_bytes() != (stage_dir / spec.name).read_bytes():
                raise AssertionError(f"Staged {spec.name} does not match the generated pack")
        require(header, "#define OPENQ4_PAK1_CLIENT_ONLY 1", "light grids are client-only")
        require(header, "#define OPENQ4_PAK2_CLIENT_ONLY 1", "loadscreens are client-only")
        require(header, "#define OPENQ4_PAK0_CLIENT_ONLY 0", "core pack is for every runtime")
    finally:
        shutil.rmtree(work, ignore_errors=True)


VERIFIED_PACK_SUPPORT = r'''
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdint.h>
#include <string>
#include <vector>

#define OPENQ4_GAMEDIR "baseoq4"
#define OPENQ4_PAK0_MD5 "00112233445566778899aabbccddeeff"
#define OPENQ4_PAK1_MD5 "ffeeddccbbaa99887766554433221100"
#define OPENQ4_PAK2_MD5 "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf"
#define OPENQ4_PACK_TABLE_ENTRIES \
	{ "pak0.pk4", OPENQ4_PAK0_MD5, 13400000, 337, false, false, "#str_231701", "core runtime content" }, \
	{ "pak1.pk4", OPENQ4_PAK1_MD5, 96000000, 5973, true, true, "#str_231702", "light grids" }, \
	{ "pak2.pk4", OPENQ4_PAK2_MD5, 530000000, 350, true, true, "#str_231703", "loadscreens" },

class idStr {
public:
	idStr() {}
	idStr( const char *text ) : s( text ? text : "" ) {}
	idStr &operator+=( const char *text ) { s += text; return *this; }
	idStr &operator+=( const idStr &text ) { s += text.s; return *this; }
	const char *c_str() const { return s.c_str(); }
	int Length() const { return static_cast<int>( s.size() ); }
	void Clear() { s.clear(); }
	static int Length( const char *text ) { return static_cast<int>( strlen( text ) ); }
	static int Icmp( const char *a, const char *b ) {
		for ( ;; ++a, ++b ) {
			const int ca = tolower( static_cast<unsigned char>( *a ) );
			const int cb = tolower( static_cast<unsigned char>( *b ) );
			if ( ca != cb || ca == 0 ) { return ca - cb; }
		}
	}
	static int Cmpn( const char *a, const char *b, int n ) {
		for ( int i = 0; i < n; ++i ) {
			if ( a[ i ] != b[ i ] || a[ i ] == '\0' ) {
				return static_cast<unsigned char>( a[ i ] ) - static_cast<unsigned char>( b[ i ] );
			}
		}
		return 0;
	}
	static int snPrintf( char *dest, int size, const char *fmt, ... ) {
		va_list args;
		va_start( args, fmt );
		const int written = vsnprintf( dest, static_cast<size_t>( size ), fmt, args );
		va_end( args );
		return written;
	}
	std::string s;
};

static const char *va( const char *fmt, ... ) {
	static char buffer[ 4096 ];
	va_list args;
	va_start( args, fmt );
	vsnprintf( buffer, sizeof( buffer ), fmt, args );
	va_end( args );
	return buffer;
}

struct idCommonStub {
	std::string log;
	void Printf( const char *fmt, ... ) {
		char buffer[ 4096 ];
		va_list args;
		va_start( args, fmt );
		vsnprintf( buffer, sizeof( buffer ), fmt, args );
		va_end( args );
		log += buffer;
	}
	void Warning( const char *fmt, ... ) {
		char buffer[ 4096 ];
		va_list args;
		va_start( args, fmt );
		vsnprintf( buffer, sizeof( buffer ), fmt, args );
		va_end( args );
		log += "WARNING: ";
		log += buffer;
	}
};

template<class T> class idList {
public:
	void		Clear() { items.clear(); }
	int			Append( const T &item ) { items.push_back( item ); return static_cast<int>( items.size() ) - 1; }
	int			Num() const { return static_cast<int>( items.size() ); }
	const T &	operator[]( int index ) const { return items[ static_cast<size_t>( index ) ]; }
	std::vector<T> items;
};
static idCommonStub commonStub;
static idCommonStub *common = &commonStub;

static int Sys_Milliseconds( void ) {
	static int now = 0;
	return now += 7;
}

struct pack_t {
	idStr	pakFilename;
	int		checksum;
	int		numfiles;
};
'''

VERIFIED_PACK_DISK = r'''
// What each pack path holds on the scripted disk. The zip checksum and entry
// count of an identity come from the loaded pack, as in the engine.
struct diskPak_t {
	openQ4PakStamp_t	stamp;
	std::string			md5;
	bool				stampable;
	bool				readable;
	bool				changesWhileHashed;
};
static std::map<std::string, diskPak_t> disk;
static int hashCalls = 0;

static bool FS_StampOpenQ4Pak( const pack_t *pack, openQ4PakStamp_t &stamp ) {
	const diskPak_t &entry = disk.at( pack->pakFilename.c_str() );
	if ( !entry.stampable ) {
		return false;
	}
	stamp = entry.stamp;
	stamp.zipChecksum = static_cast<uint32_t>( pack->checksum );
	stamp.zipFiles = static_cast<uint32_t>( pack->numfiles );
	return true;
}

static bool MD5_FileChecksum( const char *path, char digestHex[33] ) {
	hashCalls++;
	diskPak_t &entry = disk.at( path );
	if ( !entry.readable ) {
		digestHex[ 0 ] = '\0';
		return false;
	}
	if ( entry.changesWhileHashed ) {
		entry.stamp.modified++;
		entry.stamp.changed++;
	}
	memcpy( digestHex, entry.md5.c_str(), 33 );
	return true;
}

struct idCVarStub {
	std::string value;
	const char *GetString() const { return value.c_str(); }
	bool GetBool() const { return atoi( value.c_str() ) != 0; }
};

class idFileSystemLocal {
public:
	bool			ValidateOpenQ4Paks( idStr &errors, bool &onlyClientPacks );
	void			ReadVerifiedOpenQ4Paks( std::vector<verifiedOpenQ4Pak_t> &records ) {
		recordReads++;
		records.clear();
		if ( recordText.size() <= static_cast<size_t>( FS_VERIFIED_PAKS_MAX_BYTES ) ) {
			FS_ParseVerifiedPaks( recordText.c_str(), records );
		}
	}
	bool			WriteVerifiedOpenQ4Paks( const std::vector<verifiedOpenQ4Pak_t> &records ) {
		recordWrites++;
		if ( !recordWritable ) {
			return false;
		}
		FS_FormatVerifiedPaks( records, recordText );
		return true;
	}
	pack_t *		FindGamePackByName( const char *name, const char *gameDir ) {
		for ( pack_t &pack : packs ) {
			const std::string path = pack.pakFilename.c_str();
			const std::string suffix = std::string( "/" ) + gameDir + "/" + name;
			if ( path.size() >= suffix.size() && path.compare( path.size() - suffix.size(), suffix.size(), suffix ) == 0 ) {
				return &pack;
			}
		}
		return NULL;
	}
	bool			UsesOpenQ4RuntimeUnderlay( void ) const { return underlay; }
	const char *	RelativePathToOSPath( const char *relativePath, const char *basePath ) {
		osPath = std::string( "/" ) + basePath + "/baseoq4/" + relativePath;
		return osPath.c_str();
	}

	idCVarStub			fs_game{ "baseoq4" };
	idCVarStub			fs_game_base{ "" };
	idCVarStub			fs_cacheVerifiedPaks{ "1" };
	bool				underlay = false;
	bool				recordWritable = true;
	std::string			recordText;
	int					recordReads = 0;
	int					recordWrites = 0;
	std::string			osPath;
	std::vector<pack_t>	packs;
};
'''

VERIFIED_PACK_MAIN = r'''
static int failures = 0;
#define CHECK( condition, message ) \
	do { if ( !( condition ) ) { fprintf( stderr, "FAIL line %d: %s\n", __LINE__, message ); failures++; } } while ( 0 )

static const char *PAK0 = "/install/baseoq4/pak0.pk4";
static const char *PAK1 = "/install/baseoq4/pak1.pk4";
static const char *PAK2 = "/install/baseoq4/pak2.pk4";
static const char *EDITED_MD5 = "0123456789abcdef0123456789abcdef";

static openQ4PakStamp_t Stamp( uint64_t size, uint64_t modified, uint64_t changed, uint64_t fileId ) {
	openQ4PakStamp_t stamp;
	memset( &stamp, 0, sizeof( stamp ) );
	stamp.size = size;
	stamp.modified = modified;
	stamp.changed = changed;
	stamp.volume = 0x5eed;
	stamp.fileId = fileId;
	return stamp;
}

static void Install( idFileSystemLocal &fs, const char *root ) {
	const std::string pak0 = std::string( root ) + "/baseoq4/pak0.pk4";
	const std::string pak1 = std::string( root ) + "/baseoq4/pak1.pk4";
	const std::string pak2 = std::string( root ) + "/baseoq4/pak2.pk4";
	disk[ pak0 ] = diskPak_t{ Stamp( 8802959, 1000, 1001, 11 ), OPENQ4_PAK0_MD5, true, true, false };
	disk[ pak1 ] = diskPak_t{ Stamp( 96021313, 2000, 2001, 22 ), OPENQ4_PAK1_MD5, true, true, false };
	disk[ pak2 ] = diskPak_t{ Stamp( 530021313, 3000, 3001, 33 ), OPENQ4_PAK2_MD5, true, true, false };
	fs.packs.clear();
	fs.packs.push_back( pack_t{ idStr( pak0.c_str() ), 0x7d04783a, 337 } );
	fs.packs.push_back( pack_t{ idStr( pak1.c_str() ), static_cast<int>( 0xf1d23717u ), 5973 } );
	fs.packs.push_back( pack_t{ idStr( pak2.c_str() ), static_cast<int>( 0x2a2b2c2du ), 350 } );
}

struct run_t {
	bool		passed;
	bool		onlyClientPacks;
	std::string	errors;
	int			hashes;
	int			reads;
	int			writes;
	std::string	log;
};

static run_t Run( idFileSystemLocal &fs ) {
	hashCalls = 0;
	commonStub.log.clear();
	const int readsBefore = fs.recordReads;
	const int writesBefore = fs.recordWrites;
	idStr errors( "left over" );
	run_t run;
	bool onlyClientPacks = false;
	run.passed = fs.ValidateOpenQ4Paks( errors, onlyClientPacks );
	run.onlyClientPacks = onlyClientPacks;
	run.errors = errors.c_str();
	run.hashes = hashCalls;
	run.reads = fs.recordReads - readsBefore;
	run.writes = fs.recordWrites - writesBefore;
	run.log = commonStub.log;
	return run;
}

static bool Contains( const std::string &text, const char *needle ) {
	return text.find( needle ) != std::string::npos;
}

static bool SameRecord( const verifiedOpenQ4Pak_t &a, const verifiedOpenQ4Pak_t &b ) {
	return a.path == b.path && FS_SameOpenQ4PakStamp( a.stamp, b.stamp ) && idStr::Icmp( a.md5, b.md5 ) == 0;
}

static verifiedOpenQ4Pak_t Record( const char *path, uint64_t salt ) {
	verifiedOpenQ4Pak_t record;
	record.path = path;
	record.stamp = Stamp( 0x100000000ull + salt, 0x01dc5a0f3c2b1a09ull + salt, 0xffffffffffffffffull - salt, salt * 3 );
	record.stamp.volume = salt;
	record.stamp.zipChecksum = 0xf1d23717u;
	record.stamp.zipFiles = static_cast<uint32_t>( salt );
	memcpy( record.md5, OPENQ4_PAK1_MD5, 33 );
	return record;
}

static void TestRecordFormat() {
	std::vector<verifiedOpenQ4Pak_t> records, parsed;
	records.push_back( Record( "C:\\Program Files\\openQ4\\baseoq4\\pak1.pk4", 1 ) );
	records.push_back( Record( "/home/player/.local/share/openQ4 dev/baseoq4/pak0.pk4", 2 ) );
	std::string text;
	FS_FormatVerifiedPaks( records, text );
	FS_ParseVerifiedPaks( text.c_str(), parsed );
	CHECK( parsed.size() == 2 && SameRecord( parsed[ 0 ], records[ 0 ] ) && SameRecord( parsed[ 1 ], records[ 1 ] ),
		"a formatted record parses back to the same packs, spaces in paths included" );

	std::string crlf;
	for ( const char c : text ) {
		if ( c == '\n' ) {
			crlf += '\r';
		}
		crlf += c;
	}
	FS_ParseVerifiedPaks( crlf.c_str(), parsed );
	CHECK( parsed.empty(), "a header with CRLF endings is another format" );
	const std::string header = std::string( FS_VERIFIED_PAKS_HEADER ) + "\n";
	std::string crlfBody = header + crlf.substr( crlf.find( '\n' ) + 1 );
	FS_ParseVerifiedPaks( crlfBody.c_str(), parsed );
	CHECK( parsed.size() == 2 && parsed[ 0 ].path == records[ 0 ].path, "CRLF record lines lose the CR from the path" );

	FS_ParseVerifiedPaks( ( "openQ4 verified runtime packs 2\n" + text.substr( header.size() ) ).c_str(), parsed );
	CHECK( parsed.empty(), "another record version is ignored" );
	FS_ParseVerifiedPaks( FS_VERIFIED_PAKS_HEADER, parsed );
	CHECK( parsed.empty(), "a header without its newline is ignored" );
	FS_ParseVerifiedPaks( text.substr( 0, text.size() - 1 ).c_str(), parsed );
	CHECK( parsed.size() == 1, "a line without its newline is ignored" );

	const std::string line = text.substr( header.size(), text.find( '\n', header.size() ) + 1 - header.size() );
	const char *const broken[] = {
		"00112233445566778899AABBCCDDEEFF 1 2 3 4 5 6 7 /p\n",				// upper-case digest
		"0011223344556677889aabbccddeef 1 2 3 4 5 6 7 /p\n",				// short digest
		"00112233445566778899aabbccddeefg 1 2 3 4 5 6 7 /p\n",				// last digest character not hexadecimal
		"00112233445566778899aabbccddeeff 1 2 3 4 5 6 /p\n",				// a field missing
		"00112233445566778899aabbccddeeff 1 2 3 4 5 6 7 \n",				// no path
		"00112233445566778899aabbccddeeff 1 2 3 4 5 6 7\n",					// no path or separator
		"00112233445566778899aabbccddeeff 1  2 3 4 5 6 7 /p\n",				// empty field
		"00112233445566778899aabbccddeeff 1 -2 3 4 5 6 7 /p\n",				// signed field
		"00112233445566778899aabbccddeeff 1 2 3 4 5 6 7g /p\n",				// not hexadecimal
		"00112233445566778899aabbccddeeff 1 2 3 4 5 100000000 7 /p\n",		// zip checksum over 32 bits
		"00112233445566778899aabbccddeeff 1 2 3 4 5 6 100000000 /p\n",		// zip entries over 32 bits
		"00112233445566778899aabbccddeeff 10000000000000000 2 3 4 5 6 7 /p\n",	// over 64 bits
	};
	for ( const char *bad : broken ) {
		FS_ParseVerifiedPaks( ( header + bad + line ).c_str(), parsed );
		CHECK( parsed.size() == 1 && SameRecord( parsed[ 0 ], records[ 0 ] ), bad );
	}

	// BuildOSPath caps a pack path at MAX_STRING_CHARS (1024)
	std::vector<verifiedOpenQ4Pak_t> many;
	std::string longPath( 1020, 'x' );
	for ( int i = 0; i < 20; i++ ) {
		FS_RecordVerifiedPak( many, ( longPath + std::to_string( i ) ).c_str(), Record( "", static_cast<uint64_t>( i ) ).stamp, OPENQ4_PAK1_MD5 );
	}
	CHECK( many.size() == FS_VERIFIED_PAKS_MAX, "the record keeps a bounded number of packs" );
	CHECK( many[ 0 ].path == longPath + "19" && many.back().path == longPath + "4", "the newest packs stay, newest first" );
	FS_FormatVerifiedPaks( many, text );
	CHECK( text.size() <= static_cast<size_t>( FS_VERIFIED_PAKS_MAX_BYTES ), "a full record of long paths stays readable" );
	FS_ParseVerifiedPaks( text.c_str(), parsed );
	CHECK( parsed.size() == FS_VERIFIED_PAKS_MAX && SameRecord( parsed[ 3 ], many[ 3 ] ), "a full record parses back" );

	FS_RecordVerifiedPak( many, ( longPath + "10" ).c_str(), Record( "", 99 ).stamp, OPENQ4_PAK0_MD5 );
	int copies = 0;
	for ( const verifiedOpenQ4Pak_t &record : many ) {
		copies += record.path == longPath + "10" ? 1 : 0;
	}
	CHECK( copies == 1 && many[ 0 ].path == longPath + "10" && many[ 0 ].stamp.volume == 99,
		"recording a path again replaces its entry and moves it first" );
	const size_t before = many.size();
	FS_RecordVerifiedPak( many, "/bad\npath", Record( "", 5 ).stamp, OPENQ4_PAK1_MD5 );
	CHECK( many.size() == before && many[ 0 ].path == longPath + "10", "a path the line format cannot hold is not recorded" );
}

static void TestValidation() {
	{
		idFileSystemLocal fs;
		Install( fs, "/install" );
		run_t first = Run( fs );
		CHECK( first.passed && first.errors.empty() && first.hashes == 3 && first.writes == 1, "the first startup hashes every pack and records them" );
		CHECK( Contains( first.log, "Verified pk4 /install/baseoq4/pak1.pk4: hashed in" ), "a hashed pack says so" );
		run_t second = Run( fs );
		CHECK( second.passed && second.hashes == 0 && second.writes == 0, "an unchanged install is not read again" );
		CHECK( Contains( second.log, "Verified pk4 /install/baseoq4/pak1.pk4: unchanged since its checksum was recorded" ), "a recorded pack says so" );
	}

	// Every part of the identity sends the pack back to hashing, and a pack
	// edited behind it fails, even with its write time put back.
	for ( int field = 0; field < 7; field++ ) {
		for ( int edited = 0; edited < 2; edited++ ) {
			idFileSystemLocal fs;
			Install( fs, "/install" );
			Run( fs );
			diskPak_t &pak1 = disk.at( PAK1 );
			switch ( field ) {
				case 0: pak1.stamp.size++; break;
				case 1: pak1.stamp.modified++; break;
				case 2: pak1.stamp.changed++; break;
				case 3: pak1.stamp.volume++; break;
				case 4: pak1.stamp.fileId++; break;
				case 5: fs.packs[ 1 ].checksum ^= 1; break;
				default: fs.packs[ 1 ].numfiles++; break;
			}
			if ( edited ) {
				pak1.md5 = EDITED_MD5;
			}
			const run_t run = Run( fs );
			CHECK( run.hashes == 1, "a changed identity hashes that pack again" );
			if ( edited ) {
				CHECK( !run.passed && run.writes == 0, "an edited pack fails and leaves the record alone" );
				CHECK( Contains( run.errors, "checksum mismatch for baseoq4/pak1.pk4, light grids (expected ffeeddccbbaa99887766554433221100, got 0123456789abcdef0123456789abcdef from /install/baseoq4/pak1.pk4)" ),
					"the mismatch names the pack, what it holds and both digests" );
				CHECK( run.onlyClientPacks, "a light-grid mismatch can wait for the localized report" );
				CHECK( Run( fs ).hashes == 1, "and fails again at the next startup" );
			} else {
				CHECK( run.passed && run.writes == 1, "a re-hashed pack that matches is recorded again" );
				CHECK( Run( fs ).hashes == 0, "under its new identity" );
			}
		}
	}

	{
		idFileSystemLocal fs;
		Install( fs, "/install" );
		std::vector<verifiedOpenQ4Pak_t> records;
		for ( const pack_t &pack : fs.packs ) {
			openQ4PakStamp_t stamp;
			FS_StampOpenQ4Pak( &pack, stamp );
			FS_RecordVerifiedPak( records, pack.pakFilename.c_str(), stamp, EDITED_MD5 );
		}
		FS_FormatVerifiedPaks( records, fs.recordText );
		run_t run = Run( fs );
		CHECK( run.passed && run.hashes == 3 && run.writes == 1, "a pack recorded with another build's digest is hashed" );
		CHECK( Run( fs ).hashes == 0, "and recorded with this build's digest" );
		disk.at( PAK1 ).md5 = EDITED_MD5;
		FS_FormatVerifiedPaks( records, fs.recordText );
		run = Run( fs );
		CHECK( !run.passed && run.hashes == 3, "a record cannot vouch for a digest this build does not expect" );
	}

	{
		idFileSystemLocal fs;
		Install( fs, "/install" );
		Run( fs );
		fs.fs_cacheVerifiedPaks.value = "0";
		const run_t run = Run( fs );
		CHECK( run.passed && run.hashes == 3 && run.reads == 0 && run.writes == 0, "fs_cacheVerifiedPaks 0 hashes every pack and leaves the record alone" );
	}

	{
		idFileSystemLocal fs;
		Install( fs, "/install" );
		disk.at( PAK1 ).stampable = false;
		run_t run = Run( fs );
		CHECK( run.passed && run.hashes == 3 && run.writes == 1, "a pack without an identity is hashed" );
		CHECK( Run( fs ).hashes == 1, "and never recorded" );
	}

	{
		idFileSystemLocal fs;
		Install( fs, "/install" );
		disk.at( PAK1 ).changesWhileHashed = true;
		run_t run = Run( fs );
		CHECK( run.passed && run.hashes == 3, "a pack that changes while it is hashed is judged by its digest" );
		CHECK( run.writes == 1 && !Contains( fs.recordText, PAK1 ), "but gets no entry" );
		disk.at( PAK1 ).changesWhileHashed = false;
		CHECK( Run( fs ).hashes == 1, "but not recorded under the identity read before hashing" );
		CHECK( Run( fs ).hashes == 0, "until a quiet startup records it" );
	}

	{
		idFileSystemLocal fs;
		Install( fs, "/install" );
		Run( fs );
		fs.packs.erase( fs.packs.begin() + 1 );
		run_t run = Run( fs );
		CHECK( !run.passed && run.writes == 0 && Contains( run.errors, "missing baseoq4/pak1.pk4, light grids (expected checksum ffeeddccbbaa99887766554433221100)" ),
			"a missing pack fails even with a record" );
		CHECK( run.onlyClientPacks && fs_openQ4PackProblems.Num() == 1 && fs_openQ4PackProblems[ 0 ].missing,
			"a missing client-only pack is kept for the localized report" );
		fs.packs.erase( fs.packs.begin() );
		run = Run( fs );
		CHECK( !run.passed && !run.onlyClientPacks && fs_openQ4PackProblems.Num() == 2, "a missing core pack is reported at once" );
	}

	{
		idFileSystemLocal fs;
		Install( fs, "/install" );
		disk.at( PAK1 ).readable = false;
		const run_t run = Run( fs );
		CHECK( !run.passed && run.writes == 0 && Contains( run.errors, "could not read /install/baseoq4/pak1.pk4 for checksum validation" ),
			"an unreadable pack fails" );
	}

	{
		idFileSystemLocal fs;
		Install( fs, "/install" );
		fs.recordWritable = false;
		run_t run = Run( fs );
		CHECK( run.passed && run.errors.empty(), "a read-only cache path still passes good packs" );
		CHECK( Contains( run.log, "Could not record the verified pk4 checksums in /fs_cachepath/baseoq4/generated/packs/verified.txt" ),
			"and says where it could not record them" );
		CHECK( Run( fs ).hashes == 3, "so they are hashed at every startup" );
	}

	{
		idFileSystemLocal fs;
		Install( fs, "/install" );
		fs.fs_game.value = "mymod";
		run_t run = Run( fs );
		CHECK( run.passed && run.hashes == 0 && run.reads == 0, "a mod that names its own base checks no openQ4 packs" );
		fs.underlay = true;
		run = Run( fs );
		CHECK( run.passed && run.hashes == 3, "a mod over the openQ4 runtime checks them" );
		fs.underlay = false;
		fs.fs_game_base.value = "baseoq4";
		CHECK( Run( fs ).hashes == 0, "and is recorded like any other startup" );
	}

	{
		// builddir and the staged package sharing one fs_cachepath
		idFileSystemLocal staged, direct;
		Install( staged, "/install" );
		Install( direct, "/builddir" );
		Run( staged );
		direct.recordText = staged.recordText;
		CHECK( Run( direct ).hashes == 3, "another runtime's packs are not vouched for by this one's" );
		staged.recordText = direct.recordText;
		CHECK( Run( staged ).hashes == 0 && Run( direct ).hashes == 0, "both runtimes keep their entries" );
	}
}

// A dedicated server needs only the core pack.
static void TestDedicatedServer() {
	{
		idFileSystemLocal fs;
		Install( fs, "/install" );
		fs.packs.erase( fs.packs.begin() + 1, fs.packs.end() );
		const run_t run = Run( fs );
		CHECK( run.passed && run.errors.empty() && run.hashes == 1, "a server starts without light grids or loadscreens" );
		CHECK( Contains( run.log, "Client-only pk4 baseoq4/pak1.pk4 (light grids) is not installed; this server does not need it" ),
			"and says which client packs it does not carry" );
	}
	{
		idFileSystemLocal fs;
		Install( fs, "/install" );
		disk.at( PAK2 ).md5 = EDITED_MD5;
		const run_t run = Run( fs );
		CHECK( run.passed && Contains( run.log, "WARNING: Ignoring client-only pk4 that does not match this server: checksum mismatch for baseoq4/pak2.pk4" ),
			"a stale client pack does not stop a server" );
	}
	{
		idFileSystemLocal fs;
		Install( fs, "/install" );
		disk.at( PAK0 ).md5 = EDITED_MD5;
		const run_t run = Run( fs );
		CHECK( !run.passed && !run.onlyClientPacks, "a server still needs its core pack" );
	}
}

int main() {
	( void )fs_openQ4PackProblemsMayRaise;
	TestRecordFormat();
#ifdef ID_DEDICATED
	TestDedicatedServer();
#else
	TestValidation();
#endif
	if ( failures != 0 ) {
		fprintf( stderr, "%d verified-pack check(s) failed\n", failures );
		return 1;
	}
	return 0;
}
'''


def verified_pack_type(source: str, name: str) -> str:
    start = source.find(f"typedef struct {name}_s {{")
    end = source.find(f"}} {name}_t;", start)
    if start == -1 or end == -1:
        raise AssertionError(f"Missing typedef {name}_t")
    return source[start : end + len(f"}} {name}_t;")]


def validate_verified_pack_record() -> None:
    source = read("src/framework/FileSystem.cpp")
    validator = function_body(source, "bool idFileSystemLocal::ValidateOpenQ4Paks(")
    stamp = function_body(source, "static bool FS_StampOpenQ4Pak(")
    reader = function_body(source, "void idFileSystemLocal::ReadVerifiedOpenQ4Paks(")
    writer = function_body(source, "bool idFileSystemLocal::WriteVerifiedOpenQ4Paks(")
    startup = function_body(source, "void idFileSystemLocal::Startup(")

    require(
        source,
        'idCVar	idFileSystemLocal::fs_cacheVerifiedPaks( "fs_cacheVerifiedPaks", "1", CVAR_SYSTEM | CVAR_INIT | CVAR_BOOL,',
        "verified-pack record switch",
    )
    require(validator, "const bool useRecord = fs_cacheVerifiedPaks.GetBool();", "record switch gate")
    require(validator, "fs_openQ4PackProblems.Append( problem );", "pack problems kept for the localized report")
    require(validator, "const bool stamped = useRecord && FS_StampOpenQ4Pak( pack, stamp );", "identity before hashing")
    require(validator, "FS_SameOpenQ4PakStamp( recorded->stamp, stamp )", "recorded identity match")
    require(validator, "!idStr::Icmp( recorded->md5, expected.md5 )", "recorded digest must be this build's")
    require(validator, "FS_StampOpenQ4Pak( pack, after ) && FS_SameOpenQ4PakStamp( stamp, after )", "identity unchanged while hashed")
    require_order(validator, "FS_SameOpenQ4PakStamp( recorded->stamp, stamp )", "MD5_FileChecksum(", "record consulted before hashing")
    require_order(validator, "checksum mismatch for %s/%s", "FS_RecordVerifiedPak(", "only a passing pack is recorded")
    require_order(validator, "if ( errors.Length() != 0 ) {", "WriteVerifiedOpenQ4Paks( records )", "a failed check writes no record")
    require_order(startup, "ValidateOpenQ4Paks( openQ4PakErrors, onlyClientPacks )", "openQ4 runtime content packs in '%s' are missing or modified", "pack failure stays fatal")

    for token, context in (
        ("GetFileInformationByHandleEx( handle, FileBasicInfo, &basic, sizeof( basic ) )", "Windows change time"),
        ("basic.ChangeTime.QuadPart", "Windows change time"),
        ("basic.LastWriteTime.QuadPart", "Windows write time"),
        ("info.nFileIndexHigh", "Windows file id"),
        ("info.dwVolumeSerialNumber", "Windows volume"),
        ("fstat( fileno( file ), &info )", "POSIX identity from the open pack"),
        ("info.st_ctimespec", "macOS change time"),
        ("info.st_ctim", "POSIX change time"),
        ("info.st_mtim", "POSIX write time"),
        ("info.st_ino", "POSIX file id"),
        ("pack->checksum", "zip central directory CRC digest"),
        ("pack->numfiles", "zip central directory entry count"),
    ):
        require(stamp, token, context)
    reject(stamp, "st_dev", "per-mount device numbers would miss after every reboot")

    require(reader, 'RelativePathToOSPath( FS_VERIFIED_PAKS_FILE, "fs_cachepath" )', "record under fs_cachepath")
    require(reader, "OpenExplicitFileRead( osPath )", "record read from its exact path")
    reject(reader, "OpenFileRead(", "record must not come from a search path")
    reject(reader, "ReadFile(", "record must not come from a search path")
    require(writer, "GetCurrentProcessId()", "per-process staged record")
    require(writer, "getpid()", "per-process staged record")
    require(writer, 'PromoteFile( staged, FS_VERIFIED_PAKS_FILE, "fs_cachepath" )', "atomic record publication")
    require(writer, 'RemoveFileChecked( staged, "fs_cachepath" )', "staged record cleanup")

    support = "\n".join(
        verified_pack_type(source, name) for name in ("openQ4PakStamp", "verifiedOpenQ4Pak", "openQ4PackTableEntry")
    )
    table_start = source.index("static const openQ4PackTableEntry_t openQ4PackTable[] = {")
    table_end = source.index("\n", source.index("static const int openQ4PackTableCount", table_start))
    support += "\n" + source[table_start:table_end]
    problems_start = source.index("typedef struct openQ4PackProblem_s {")
    problems_end = source.index("\n", source.index("static bool\t\t\t\t\t\t\tfs_openQ4PackProblemsMayRaise", problems_start))
    problems = source[problems_start:problems_end]
    constants = "\n".join(
        line for line in source.splitlines() if line.startswith("static const") and "FS_VERIFIED_PAKS_" in line
    )
    if constants.count("\n") != 3:
        raise AssertionError("Expected the four FS_VERIFIED_PAKS_ constants")
    helpers = "\n".join(
        function_body(source, signature)
        for signature in (
            "static bool FS_SameOpenQ4PakStamp(",
            "static bool FS_IsVerifiedPakHexDigit(",
            "static bool FS_ParseVerifiedPakNumber(",
            "static bool FS_ParseVerifiedPakLine(",
            "static void FS_ParseVerifiedPaks(",
            "static void FS_FormatVerifiedPaks(",
            "static const verifiedOpenQ4Pak_t *FS_FindVerifiedPak(",
            "static void FS_RecordVerifiedPak(",
        )
    )
    program_text = "\n".join(
        (VERIFIED_PACK_SUPPORT, support, constants, helpers, problems, VERIFIED_PACK_DISK, validator, VERIFIED_PACK_MAIN)
    )

    compiler = next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("a C++ compiler is required for the verified-pack record harness")
    flags = ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-function"]
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="verified-pack-", dir=ROOT / ".tmp") as directory:
        program = Path(directory) / "verified_pack.cpp"
        binary = Path(directory) / "verified_pack.exe"
        program.write_text(program_text, encoding="utf-8")
        subprocess.run([compiler, *flags, str(program), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        dedicated = Path(directory) / "verified_pack_dedicated.exe"
        subprocess.run([compiler, *flags, "-DID_DEDICATED", str(program), "-o", str(dedicated)], check=True)
        subprocess.run([str(dedicated)], check=True)


def validate_validation_coverage() -> None:
    validator = read("tools/validation/openq4_validate.py")
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")

    for haystack, context in (
        (validator, "validation runner"),
        (push, "push verification workflow"),
        (commit, "commit validation workflow"),
    ):
        require(haystack, "openq4_pure_pack.py", context)

    for haystack, context in (
        (push, "push verification workflow"),
        (commit, "commit validation workflow"),
    ):
        require(haystack, "build_pak0.py", context)
        require(haystack, "build_openq4_pack.py", context)
        require(haystack, "generate_pak_header.py", context)
        require(haystack, "list_pak_sources.py", context)
        require(haystack, "write_pak_manifest.py", context)
        require(haystack, "openq4_pak.py", context)


def main() -> None:
    validate_filesystem_pure_pack_contract()
    validate_install_error_console_contract()
    validate_packager_pure_pack_contract()
    validate_build_pak0_contract()
    validate_verified_pack_record()
    validate_validation_coverage()
    print("openq4_pure_pack: ok")


if __name__ == "__main__":
    main()
