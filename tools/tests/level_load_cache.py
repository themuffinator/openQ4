#!/usr/bin/env python3
"""Static integration checks for Milestone B loading/cache modernization."""

from __future__ import annotations

import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT.parent / "openQ4-game")).resolve()


def read(root: Path, relative: str) -> str:
    path = root / relative
    if not path.is_file():
        raise AssertionError(f"Required file not found: {path}")
    return path.read_text(encoding="utf-8")


def require(text: str, needle: str, context: str) -> None:
    if needle not in text:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_order(text: str, first: str, second: str, context: str) -> None:
    first_index = text.find(first)
    second_index = text.find(second)
    if first_index < 0 or second_index < 0 or first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def validate_abi_contract() -> None:
    engine_fs = read(ROOT, "src/framework/FileSystem.h")
    game_fs = read(GAME_ROOT, "src/framework/FileSystem.h")
    for token in (
        "LEVEL_LOAD_RESOURCE_RENDER_MODEL = 1",
        "GENERATED_CACHE_RENDER_MODEL = 1",
        "BeginLevelLoadCache",
        "FinishLevelLoadCache",
        "CancelLevelLoadCache",
        "RecordLevelLoadResource",
        "OpenGeneratedCacheRead",
        "WriteGeneratedCache",
        "DiscardGeneratedCache",
    ):
        require(engine_fs, token, "engine filesystem ABI")
        require(game_fs, token, "game filesystem ABI")
    require(
        read(ROOT, "src/renderer/RenderModuleAPI.h"),
        "#define RENDER_API_VERSION\t\t\t14",
        "renderer module ABI",
    )
    require(
        read(GAME_ROOT, "src/game/Game.h"),
        "GAME_API_VERSION\t\t= 48",
        "game module ABI",
    )


def validate_manifest_and_envelope_contract() -> None:
    header = read(ROOT, "src/framework/LevelLoadCacheFormat.h")
    source = read(ROOT, "src/framework/LevelLoadCacheFormat.cpp")
    native_test = read(ROOT, "tools/tests/native/LevelLoadCacheFormatTest.cpp")
    meson = read(ROOT, "meson.build")
    for token in (
        "SourceIdentity",
        "containerPk4Checksum",
        "EnvelopeExpectation",
        "ManifestExpectation",
        "Hash\t\t\t\tgameMode",
        "Hash\t\t\t\tentityFilter",
        "DecodeLimits",
        "DecodeEnvelopeForKey",
        "DecodeManifestForKey",
        "ValidateDecodedPayload",
    ):
        require(header, token, "portable cache format API")
    for token in (
        "INTEGRITY_MISMATCH",
        "TRAILING_DATA",
        "SIZE_LIMIT_EXCEEDED",
        "CanonicalizeManifest",
    ):
        require(header + source, token, "hardened format implementation")
    require(native_test, "ExerciseEnvelopeFailures", "native malformed-cache coverage")
    require(native_test, "ExerciseManifestFailures", "native manifest corruption coverage")
    require(meson, "openq4-level-load-cache-format", "native Meson cache-format test")


def validate_pipeline_and_lifecycle() -> None:
    pipeline = read(ROOT, "src/framework/LevelLoadPipeline.cpp")
    pipeline_test = read(ROOT, "tools/tests/native/LevelLoadPipelineTest.cpp")
    meson = read(ROOT, "meson.build")
    manager = read(ROOT, "src/framework/LevelLoadCacheManager.cpp")
    filesystem = read(ROOT, "src/framework/FileSystem.cpp")
    session = read(ROOT, "src/framework/Session.cpp")

    for token in (
        "maxEntries",
        "maxSourceBytes",
        "maxTotalBytes",
        "readChunkBytes",
        "maxDecodedBytes",
        "decodeChunkBytes",
        "IsCancellationRequested",
        "ReportDecodedBytes",
        "idLevelLoadDecodedSource",
        "payloadOffset",
        "frameUnitCount",
        "ComputeFramingSeal",
        "idLevelLoadDecodeSourceFrame",
        "synchronousFallback",
        "DrainOpenFiles",
    ):
        require(pipeline, token, "bounded cancellable pipeline")
    for token in (
        "ExerciseBoundedSynchronousReplay",
        "ExerciseCooperativeCancellation",
        "ExerciseCooperativeDecodeCancellation",
        "ExerciseMalformedFramingFallback",
        "ExerciseProductionFramingValidation",
        "ExerciseSetupFailureHandleRecovery",
        "ExerciseQueueSaturationFallback",
    ):
        require(pipeline_test, token, "native level-load pipeline coverage")
    require(meson, "openq4-level-load-pipeline", "native Meson level-load pipeline test")
    require(filesystem, "std::lock_guard<std::mutex> lock( readCountMutex )", "worker read accounting")
    require(filesystem, "std::this_thread::get_id() == fileSystemMainThread", "main-thread pacifier")
    require(filesystem, "UsePreloadedLevelLoadSource", "source-authoritative preload substitution")
    if filesystem.count("levelLoadCache = new idLevelLoadCacheManager( this );") != 2:
        raise AssertionError("level-load coordinator must be created by initial startup and restart")

    for token in (
        '"com_levelLoadModernization", "0"',
        "!com_levelLoadModernization.GetBool()",
        '"generated/manifests/"',
        'subdirectory = "models"',
        'subdirectory = "worlds"',
        'subdirectory = "collision"',
        "DecodeManifestForKey",
        "DecodeEnvelopeForKey",
        "RemoveFileChecked",
        "file->Sync()",
        "PromoteFile",
        "OpenExplicitFileRead",
        "OpenFileRead( normalized.c_str(), false )",
        "completedGeneration = true",
        "WriteLearnedManifest",
        "OpenPreloadedSource",
        "DecodePipelineSource",
        "&ReadPipelineSource, nullptr, &DecodePipelineSource",
        "idLevelLoadDecodeSourceFrame",
        "HASH_DOMAIN",
        "read/decompress stage",
        "decodeBudgetBytes",
        'extension == ".md5animc"',
        'extension == ".md5rc"',
        'extension == ".md5rmeshc"',
        'extension == ".procc"',
        'extension == ".md5rprocc"',
        "compiledName += Lexer::sCompiledFileSuffix.c_str()",
        "Generated cache owner payload rejected and removed",
        "generatedCorruptions.fetch_add",
        "HashHex( expected.gameMode )",
        "HashHex( expected.entityFilter )",
        '"com_levelLoadPreloadMaxEntries", "64"',
        "idCmdSystem::ArgCompletion_Integer<1,64>",
    ):
        require(manager, token, "level-load coordinator")
    for token in (
        "search->pack->checksum",
        "search->pack->length",
        "search->pack->numfiles",
        "serverPaks",
    ):
        require(filesystem, token, "exact active content signature")

    unload = session[session.index("void idSessionLocal::UnloadMap()") :]
    require_order(unload, "CancelLevelLoadCache", "game->MapShutdown", "map teardown join")
    execute_start = session.index("void idSessionLocal::ExecuteMapChange")
    execute_end = session.index("idSessionLocal::TakeNotes", execute_start)
    execute = session[execute_start:execute_end]
    if execute.count("fileSystem->ResetReadCount();") != 1:
        raise AssertionError("map-change read accounting must have exactly one reset")
    require_order(
        execute,
        "fileSystem->ResetReadCount();",
        "fileSystem->BeginLevelLoadCache",
        "read accounting reset before worker publication",
    )
    require_order(execute, "BeginLevelLoadCache", "rw->InitFromMap", "generation start")
    require_order(execute, "FinishLevelLoadCache", "renderSystem->EndLevelLoad", "main-thread join")
    require_order(execute, "renderSystem->EndLevelLoad", "FS_ReleaseLevelLoadCache", "render replay retention")
    require_order(execute, "uiManager->EndLevelLoad", "FS_ReleaseLevelLoadCache", "final replay consumer")


def validate_resource_consumers() -> None:
    model = "\n".join(
        read(ROOT, path)
        for path in (
            "src/renderer/Model.cpp",
            "src/renderer/Model_md5.cpp",
            "src/renderer/Model_md5r.cpp",
        )
    )
    world = read(ROOT, "src/renderer/RenderWorld_load.cpp")
    collision = "\n".join(
        read(ROOT, path)
        for path in (
            "src/cm/CollisionModel_files.cpp",
            "src/cm/CollisionModel_load.cpp",
        )
    )
    for source, kind, context in (
        (model, "GENERATED_CACHE_RENDER_MODEL", "render-model cache"),
        (world, "GENERATED_CACHE_RENDER_WORLD", "render-world cache"),
        (collision, "GENERATED_CACHE_COLLISION_MODEL", "collision cache"),
    ):
        require(source, kind, context)
        require(source, "OpenGeneratedCacheRead", context)
        require(source, "WriteGeneratedCache", context)
        require(source, "DiscardGeneratedCache", context)
    require(model, "WriteLevelLoadCachePayload", "render-model bounded payload")
    require(
        model,
        "edge.p2 > batch.silTraceGeoSpec.primitiveCount",
        "MD5R open-edge sentinel validation",
    )
    require(world, "WriteLevelLoadCachePayload", "render-world bounded payload")
    require(world, "R_RenderWorldCacheWriteShadowModel", "render-world shadow-only payload writer")
    require(world, "R_RenderWorldCacheReadShadowModel", "render-world shadow-only payload reader")
    require(world, "RENDER_WORLD_CACHE_MODEL_SHADOW", "render-world shadow payload discriminator")
    require(world, "indexes[i] >= numVerts", "render-world shadow index validation")
    require(collision, "WriteGeneratedCollisionCache", "collision bounded payload writer")
    require(collision, "LoadGeneratedCollisionCache", "collision bounded payload reader")
    require(collision, "CM_CACHE_MAX_ALLOCATION_BYTES", "collision allocation budget")
    require_order(
        collision,
        'cvarSystem->GetCVarBool( "com_binaryRead" )',
        "compiledProcPath += Lexer::sCompiledFileSuffix",
        "collision compiled-proc enablement",
    )
    require_order(
        collision,
        "compiledProcPath += Lexer::sCompiledFileSuffix",
        "OpenFileRead( compiledProcPath.c_str(), false )",
        "collision compiled-proc open",
    )
    require_order(
        collision,
        "OpenFileRead( compiledProcPath.c_str(), false )",
        "OpenFileRead( sourceProcPath.c_str(), false )",
        "collision text-proc fallback",
    )
    for token in (
        "resolvedProcPath = compiledProcPath",
        '"mode=map-proc;proc=%s;length=%d;timestamp=%lld;container=%08x;loose-crc=%08x;complete=%d"',
    ):
        require(collision, token, "collision selected-proc cache key")

    image = read(ROOT, "src/renderer/ImageManager.cpp")
    sound = read(ROOT, "src/sound/snd_system.cpp")
    sp_anim = read(GAME_ROOT, "src/game/anim/Anim.cpp")
    require(image, "LEVEL_LOAD_RESOURCE_IMAGE", "image semantic manifest hook")
    require(sound, "LEVEL_LOAD_RESOURCE_SOUND", "sound semantic manifest hook")
    require(sp_anim, "LEVEL_LOAD_RESOURCE_ANIMATION", "animation semantic manifest hook")
    require(sp_anim, "GENERATED_ANIM_VERSION = 3", "hardened animation cache v3")
    require(sp_anim, "OpenExplicitFileRead", "private animation cache read")
    require(sp_anim, "PromoteFile", "atomic animation cache write")


def main() -> None:
    validate_abi_contract()
    validate_manifest_and_envelope_contract()
    validate_pipeline_and_lifecycle()
    validate_resource_consumers()
    print("level_load_cache: ok")


if __name__ == "__main__":
    main()
