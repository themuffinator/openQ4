#!/usr/bin/env python3
"""Regression checks for the generated MD5 animation cache contract."""

from __future__ import annotations

import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)).resolve()


def read(root: Path, relative_path: str) -> str:
    path = root / relative_path
    if not path.is_file():
        raise AssertionError(f"Required source file not found: {path}")
    return path.read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_order(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index == -1 or second_index == -1 or first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def validate_engine_file_identity_contract() -> None:
    engine_header = read(ROOT, "src/framework/File.h")
    game_header = read(GAME_LIBS_ROOT, "src/framework/File.h")
    engine_filesystem_header = read(ROOT, "src/framework/FileSystem.h")
    game_filesystem_header = read(GAME_LIBS_ROOT, "src/framework/FileSystem.h")
    file_source = read(ROOT, "src/framework/File.cpp")
    filesystem_source = read(ROOT, "src/framework/FileSystem.cpp")

    for source, context in (
        (engine_header, "engine file interface"),
        (game_header, "GameLibs file interface"),
    ):
        require(
            source,
            "virtual int\t\t\t\tGetContainerChecksum( void ) const { return 0; }",
            context,
        )
        require(
            source,
            "virtual int\t\t\t\tGetContainerChecksum( void ) const { return containerChecksum; }",
            context,
        )
        require(source, "int\t\t\t\t\t\tcontainerChecksum;", context)
        require_order(
            source,
            "virtual void\t\t\tReadSyncId",
            "virtual void\t\t\tMakeReadOnly",
            f"{context} append-only staging ABI",
        )

    for source, context in (
        (engine_filesystem_header, "engine filesystem interface"),
        (game_filesystem_header, "GameLibs filesystem interface"),
    ):
        require_order(
            source,
            "virtual bool\t\t\tInProductionMode() = 0;",
            "virtual void\t\t\tBeginLevelLoadCache",
            f"{context} append-only level-load ABI",
        )

    require(file_source, "containerChecksum = 0;", "PK4 file construction")
    require(
        filesystem_source,
        "file->containerChecksum = pak->checksum;",
        "PK4 file source identity",
    )


def validate_game_cache_contract() -> None:
    sp = read(GAME_LIBS_ROOT, "src/game/anim/Anim.cpp")
    mp = read(GAME_LIBS_ROOT, "src/game/anim/Anim.cpp")

    sp_shared = sp[sp.index("bool idAnimManager::forceExport") :]
    mp_shared = mp[mp.index("bool idAnimManager::forceExport") :]
    if sp_shared != mp_shared:
        raise AssertionError("SP and MP animation cache implementations have drifted")

    for token in (
        'g_useGeneratedAnimCache( "g_useGeneratedAnimCache", "1"',
        'g_writeGeneratedAnimCache( "g_writeGeneratedAnimCache", "1"',
        'path = "generated/animations/";',
        "GENERATED_ANIM_MAGIC",
        "GENERATED_ANIM_END_MAGIC",
        "GENERATED_ANIM_VERSION = 3",
        "MAX_GENERATED_ANIM_FRAMES",
        "MAX_GENERATED_ANIM_JOINTS",
        "MAX_GENERATED_ANIM_FRAME_RATE",
        "MAX_GENERATED_ANIM_DATA_BYTES",
        "MAX_GENERATED_ANIM_RECORD_BYTES",
        "GENERATED_ANIM_CRC_TRAILER_BYTES",
        "info.length = source->Length();",
        "info.timestamp = source->Timestamp();",
        "info.containerChecksum = source->GetContainerChecksum();",
        'cvarSystem->GetCVarBool( "com_binaryRead" )',
        "selectedPath.Append( Lexer::sCompiledFileSuffix );",
        "NormalizeGeneratedAnimSourcePath( selectedPath, info.normalizedPath )",
        "NormalizeGeneratedAnimSourcePath",
        "normalized.BackSlashesToSlashes();",
        "normalized.ToLower();",
        "normalized[ 0 ] == '/'",
        "c == 127 || c == ':'",
        "segmentLength == 0",
        "GeneratedAnimSegmentIsWindowsDevice",
        "const unsigned char digit = static_cast<unsigned char>( segment[ 3 ] );",
        "digit == 0xB9 || digit == 0xB2 || digit == 0xB3",
        "stemLength == 5 && digit == 0xC2",
        "static_cast<unsigned char>( segment[ 4 ] ) == 0xB9",
        "static_cast<unsigned char>( segment[ 4 ] ) == 0xB2",
        "static_cast<unsigned char>( segment[ 4 ] ) == 0xB3",
        "CRC32_UpdateChecksum( checksum, buffer, readBytes );",
        "sourceLength == sourceInfo.length",
        "sourceContainerChecksum == sourceInfo.containerChecksum",
        "sourceContentChecksum == sourceInfo.contentChecksum",
        "sourceTimestamp == sourceInfo.timestamp",
        "sourcePath.Cmp( sourceInfo.normalizedPath ) == 0",
        "position < 0 || fileLength < position",
        "valueLength > MAX_STRING_CHARS",
        "LittleRevBytes( values, sizeof( float ), count );",
        "if ( !Swap_IsBigEndian() )",
        "GeneratedAnimFloatsAreFinite",
        "!FLOAT_IS_NAN( value )",
        "CalculateGeneratedAnimLength",
        "frameRate > MAX_GENERATED_ANIM_FRAME_RATE",
        "componentCount <= 0x7fffffff",
        "dataBytes <= MAX_GENERATED_ANIM_DATA_BYTES",
        "GeneratedAnimComponentCount",
        "jointComponentCount <= cachedNumAnimatedComponents - joint.firstComponent",
        "cachedBounds[ i ][ 0 ][ axis ] <= cachedBounds[ i ][ 1 ][ axis ]",
        "quaternionLengthSqr - 1.0f",
        "storedLength == (unsigned int)recordLength",
        "storedChecksum == CRC32_BlockChecksum( diskBytes.Ptr(), recordLength )",
        "fileSystem->GetNewFileMemory();",
        "cache->MakeReadOnly();",
        "endMagic == GENERATED_ANIM_END_MAGIC",
        "cache->Tell() == cache->Length()",
        'fileSystem->OpenExplicitFileRead( cacheOSPath )',
        'fileSystem->RemoveFileChecked( cachePath, "fs_savepath" );',
        "stagedCache->Sync();",
        'fileSystem->PromoteFile( stagedPath, cachePath, "fs_savepath" )',
        "LEVEL_LOAD_RESOURCE_ANIMATION",
        "baseFrame.Clear();",
        "baseFrame.Allocated()",
    ):
        require(sp, token, "generated animation cache implementation")

    require_order(
        sp,
        "if ( !GetGeneratedAnimPath( filename, cachePath ) )",
        'fileSystem->RelativePathToOSPath( cachePath, "fs_savepath" )',
        "explicit cache-read path validation",
    )
    require_order(
        sp,
        "storedChecksum == CRC32_BlockChecksum( diskBytes.Ptr(), recordLength )",
        "cache = fileSystem->GetNewFileMemory();",
        "integrity-before-parse staging",
    )
    require_order(
        sp,
        'cvarSystem->GetCVarBool( "com_binaryRead" )',
        "selectedPath.Append( Lexer::sCompiledFileSuffix );",
        "compiled animation source selection",
    )
    require_order(
        sp,
        "selectedPath.Append( Lexer::sCompiledFileSuffix );",
        "if ( source == NULL )",
        "compiled-source precedence before ASCII fallback",
    )
    require_order(
        sp,
        "NormalizeGeneratedAnimSourcePath( selectedPath, info.normalizedPath )",
        "sourcePath.Cmp( sourceInfo.normalizedPath ) == 0",
        "selected parser-source identity publication",
    )

    require_order(
        sp,
        'cvarSystem->GetCVarBool( "com_levelLoadModernization" )',
        "g_useGeneratedAnimCache.GetBool() && LoadGeneratedAnim( filename )",
        "cache-before-source load order",
    )
    require_order(
        sp,
        "g_useGeneratedAnimCache.GetBool() && LoadGeneratedAnim( filename )",
        "parser.LoadFile( filename )",
        "cache-before-source load order",
    )
    require_order(
        sp,
        'cvarSystem->GetCVarBool( "com_levelLoadModernization" )',
        "g_writeGeneratedAnimCache.GetBool()",
        "default-off animation-cache write gate",
    )
    require_order(
        sp,
        "g_writeGeneratedAnimCache.GetBool()",
        "WriteGeneratedAnim( filename );",
        "source-parse cache write control",
    )
    require_order(
        sp,
        "CalculateGeneratedAnimLength( numFrames, frameRate, animLength )",
        "WriteGeneratedAnim( filename );",
        "source-parse cache write order",
    )


def validate_documentation_contract() -> None:
    guide = read(ROOT, "docs/user/level-load-cache.md")
    release = read(ROOT, "docs/dev/release-completion.md")
    readme = read(ROOT, "README.md")

    for token in (
        "<fs_savepath>/baseoq4/generated/animations/",
        "g_useGeneratedAnimCache 1",
        "g_writeGeneratedAnimCache 1",
        "stale, truncated, corrupt, or mismatched cache is ignored",
        "x64 and ARM64 builds use the same format",
    ):
        require(guide, token, "level-load cache guide")

    require(readme, "docs/user/level-load-cache.md", "README player-guide index")
    require(
        release,
        "Animation-heavy level loads now build validated, endian-stable generated MD5 animation caches",
        "release completion notes",
    )


def main() -> None:
    validate_engine_file_identity_contract()
    validate_game_cache_contract()
    validate_documentation_contract()
    print("generated_animation_cache: ok")


if __name__ == "__main__":
    main()
