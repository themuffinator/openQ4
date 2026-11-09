#!/usr/bin/env python3
"""Static integration checks for Milestone B loading/cache modernization."""

from __future__ import annotations

import os
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)).resolve()


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


def forbid(text: str, needle: str, context: str) -> None:
    if needle in text:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def function_body(text: str, head: str) -> str:
    """Return the definition that starts with head, through its closing brace."""
    start = text.find(head)
    if start < 0:
        raise AssertionError(f"Missing definition of {head!r}")
    depth = 0
    for index in range(text.index("{", start), len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]
    raise AssertionError(f"Unbalanced body for {head!r}")


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
        "GeneratedCacheReadsEnabled",
        "GeneratedCacheWritesEnabled",
    ):
        require(engine_fs, token, "engine filesystem ABI")
        require(game_fs, token, "game filesystem ABI")
    # the enable queries were appended after the campaign queries; older game
    # modules keep the complete historical prefix of the shared vtable
    require_order(
        engine_fs,
        "virtual const char *GetActiveGameDir() const = 0;",
        "virtual bool\t\t\tGeneratedCacheReadsEnabled( generatedCacheKind_t kind ) const = 0;",
        "append-only filesystem slots",
    )
    require(
        read(ROOT, "src/renderer/RenderModuleAPI.h"),
        "#define RENDER_API_VERSION\t\t\t33",
        "renderer module ABI",
    )
    require(
        read(GAME_ROOT, "src/game/Game.h"),
        "GAME_API_VERSION\t\t= 58",
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

    # A disabled cache must cost a cvar check: the gates come before the
    # content key walks the search paths.
    for head, gate in (
        ("idFile *idFileSystemLocal::OpenGeneratedCacheRead(", "!GeneratedCacheReadsEnabled( kind )"),
        ("bool idFileSystemLocal::WriteGeneratedCache(", "!GeneratedCacheWritesEnabled( kind )"),
    ):
        require_order(function_body(filesystem, head), gate, "BuildLevelLoadContentKey( contentKey );",
                      "generated-cache gate before the content key")
    require(manager, "return GeneratedCacheReadsEnabled( kind ) && com_levelLoadCacheWrite.GetBool();",
            "writes require the read gate plus com_levelLoadCacheWrite")
    open_read = function_body(manager, "idFile *idLevelLoadCacheManager::OpenGeneratedCacheRead(")
    require_order(open_read, "!GeneratedCacheReadsEnabled( kind )", "OpenFileRead( normalized.c_str(), false )",
                  "coordinator read gate")
    # DecodeEnvelope already hashed the uncompressed payload against its digest
    forbid(open_read, "idLevelLoadCache::ValidateDecodedPayload(", "uncompressed generated-cache read (third SHA-256 pass)")
    write = function_body(manager, "bool idLevelLoadCacheManager::WriteGeneratedCache(")
    require_order(write, "!GeneratedCacheWritesEnabled( kind )", "OpenFileRead( normalized.c_str(), false )",
                  "coordinator write gate")
    forbid(write, "ComputeHash( payload, payloadBytes )", "uncompressed generated-cache write (EncodeEnvelope hashes it)")

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
    # Every model load runs these helpers; with the cache off neither may build a
    # payload, and the read still records the resource for the learned manifest.
    model_cpp = read(ROOT, "src/renderer/Model.cpp")
    try_read = function_body(model_cpp, "bool R_TryReadGeneratedRenderModelCache(")
    require_order(try_read, "fileSystem->RecordLevelLoadResource( LEVEL_LOAD_RESOURCE_RENDER_MODEL,",
                  "fileSystem->GeneratedCacheReadsEnabled( GENERATED_CACHE_RENDER_MODEL )",
                  "render-model resource recorded before the read gate")
    require_order(try_read, "fileSystem->GeneratedCacheReadsEnabled( GENERATED_CACHE_RENDER_MODEL )",
                  "fileSystem->OpenGeneratedCacheRead( GENERATED_CACHE_RENDER_MODEL,", "render-model read gate")
    write_model = function_body(model_cpp, "void R_WriteGeneratedRenderModelCache(")
    require_order(write_model, "fileSystem->GeneratedCacheWritesEnabled( GENERATED_CACHE_RENDER_MODEL )",
                  "WriteLevelLoadCachePayload( *payload )", "render-model write gate before serialization")
    require_order(world, "fileSystem->GeneratedCacheWritesEnabled( GENERATED_CACHE_RENDER_WORLD )",
                  "WriteLevelLoadCachePayload( *cachePayload )", "render-world write gate before serialization")
    write_collision = function_body(collision, "void idCollisionModelManagerLocal::WriteGeneratedCollisionCache(")
    require_order(write_collision, "fileSystem->GeneratedCacheWritesEnabled( GENERATED_CACHE_COLLISION_MODEL )",
                  "cmCacheWriter_t writer( payload );", "collision write gate before serialization")

    # The MD5R codec streams whole arrays like the static codec, in the byte
    # layout the per-element codec wrote (so its parser version did not move).
    md5r = read(ROOT, "src/renderer/Model_md5r.cpp")
    for head, call in (
        ("static bool R_MD5RCacheReadVec4List(", "reader.ReadFloatArray( reinterpret_cast<float *>( list.Ptr() ), count * 4 )"),
        ("static bool R_MD5RCacheReadVec3List(", "reader.ReadFloatArray( reinterpret_cast<float *>( list.Ptr() ), count * 3 )"),
        ("static bool R_MD5RCacheReadDwordList(", "reader.ReadIntArray( reinterpret_cast<int *>( list.Ptr() ), count )"),
        ("static bool R_MD5RCacheReadFloatList(", "reader.ReadFloatArray( list.Ptr(), count )"),
        ("static bool R_MD5RCacheReadJointMat(", "reader.ReadFloatArray( mat.ToFloatPtr(), 12 )"),
        ("static bool R_MD5RCacheWriteVec4List(", "writer.WriteFloatArray( reinterpret_cast<const float *>( list.Ptr() ), list.Num() * 4 )"),
        ("static bool R_MD5RCacheWriteVec3List(", "writer.WriteFloatArray( reinterpret_cast<const float *>( list.Ptr() ), list.Num() * 3 )"),
        ("static bool R_MD5RCacheWriteDwordList(", "writer.WriteIntArray( reinterpret_cast<const int *>( list.Ptr() ), list.Num() )"),
        ("static bool R_MD5RCacheWriteFloatList(", "writer.WriteFloatArray( list.Ptr(), list.Num() )"),
        ("static bool R_MD5RCacheWriteJointMat(", "writer.WriteFloatArray( mat.ToFloatPtr(), 12 )"),
    ):
        require(function_body(md5r, head), call, f"MD5R bulk codec {head}")
    read_indexes = function_body(md5r, "static bool R_MD5RCacheReadIndexBuffer(")
    require_order(read_indexes, "reader.ReadIntArray( reinterpret_cast<int *>( buffer.indices.Ptr() ), buffer.numIndices )",
                  "index < 0 || index >= MD5R_CACHE_MAX_VERTICES || ( buffer.bitDepth == 16 && index > 0xffff )",
                  "MD5R bulk index read keeps the range check")
    require(function_body(md5r, "static bool R_MD5RCacheWriteIndexBuffer("),
            "writer.WriteIntArray( reinterpret_cast<const int *>( buffer.indices.Ptr() ), buffer.numIndices )",
            "MD5R bulk index write")
    require(md5r, "writer.WriteIntArray( reinterpret_cast<const int *>( resolvedSilEdges.Ptr() ), resolvedSilEdges.Num() * 4 )",
            "MD5R bulk silhouette-edge write")
    require_order(function_body(md5r, "bool rvRenderModelMD5R::ReadLevelLoadCachePayload("),
                  "reader.ReadIntArray( reinterpret_cast<int *>( staged.silEdges.Ptr() ), silEdgeCount * 4 )",
                  "edge.p1 < 0 || edge.p1 > MD5R_CACHE_MAX_INDICES / 3", "MD5R bulk silhouette-edge read keeps the range check")
    for needle in ("reader.ReadVec4( list[i] )", "reader.ReadVec3( list[i] )", "reader.ReadUnsignedInt( list[i] )",
                   "reader.ReadInt( edge.p1 )", "writer.WriteInt( edge.p1 )"):
        forbid(md5r, needle, "MD5R per-element codec")
    for assertion in (
        "static_assert( sizeof( idVec4 ) == 4 * sizeof( float ),",
        "static_assert( sizeof( idVec3 ) == 3 * sizeof( float ),",
        "static_assert( sizeof( dword ) == sizeof( int ),",
        "static_assert( sizeof( glIndex_t ) == sizeof( int ),",
        "static_assert( sizeof( silEdge_t ) == 4 * sizeof( int ),",
    ):
        require(md5r, assertion, "MD5R bulk codec layout")
    require(function_body(model_cpp, "bool idRenderModelCacheReader::ReadFloatArray("),
            "!R_RenderModelCacheFloatIsFinite( values[i] )", "bulk float reads stay finite-checked")

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


def validate_surface_ids() -> None:
    # The static and world payloads store each surface's id, and the ambient
    # pass tests suppressSurfaceMask against it, so no modelSurface_t may start
    # as stack garbage: a bare declaration needs a memset or all four fields
    # assigned straight after it.
    declaration = re.compile(r"\bmodelSurface_t\s+(\w+)\s*[;,]")
    fields = ("id", "shader", "geometry", "mOriginalSurfaceName")
    for folder in ("src/renderer", "src/bse"):
        for path in sorted((ROOT / folder).rglob("*.cpp")):
            text = path.read_text(encoding="utf-8", errors="replace")
            for match in declaration.finditer(text):
                name = match.group(1)
                following = "\n".join(text[match.end() :].split("\n", 7)[:7])
                if f"memset( &{name}, 0," in following or all(f"{name}.{field} =" in following for field in fields):
                    continue
                line = text.count("\n", 0, match.start()) + 1
                raise AssertionError(f"{path.relative_to(ROOT).as_posix()}:{line}: modelSurface_t {name} starts uninitialised")

    # idRenderModelStatic::GetSurfaceMask sets mask bits by surface index, so
    # parsed and back-side surfaces take their index as their id.
    world = read(ROOT, "src/renderer/RenderWorld_load.cpp")
    require(function_body(world, "idRenderModel *idRenderWorldLocal::ParseModel("),
            "surf.id = model->NumSurfaces();", "proc model surface ids")
    require(read(ROOT, "src/renderer/Model.cpp"), "newSurf.id = NumSurfaces();", "back-side surface ids")


def main() -> None:
    validate_abi_contract()
    validate_manifest_and_envelope_contract()
    validate_pipeline_and_lifecycle()
    validate_resource_consumers()
    validate_surface_ids()
    print("level_load_cache: ok")


if __name__ == "__main__":
    main()
