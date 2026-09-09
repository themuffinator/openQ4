#!/usr/bin/env python3
"""Regression checks for relative filesystem mutation qpath safety."""

import hashlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INVALID_WINDOWS_CHARACTERS = set('<>:"\\|?*')
WINDOWS_DEVICE_NAMES = {"con", "prn", "aux", "nul"}
WINDOWS_DEVICE_NAMES.update(f"com{digit}" for digit in "123456789¹²³")
WINDOWS_DEVICE_NAMES.update(f"lpt{digit}" for digit in "123456789¹²³")


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def require_order(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index == -1 or second_index == -1:
        raise AssertionError(f"Missing ordered symbols {first!r} and/or {second!r} in {context}")
    if first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    depth = 0
    for index in range(start, len(source)):
        character = source[index]
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find end of function {signature!r}")


def is_safe_relative_write_path_model(relative_path: str | None) -> bool:
    if not relative_path or relative_path.startswith(("/", "\\")):
        return False
    if any(ord(character) < 32 or character in INVALID_WINDOWS_CHARACTERS for character in relative_path):
        return False

    for segment in relative_path.split("/"):
        if not segment or segment in (".", "..") or segment.startswith(" ") or segment.endswith((".", " ")):
            return False
        stem = segment.split(".", 1)[0].lower()
        if stem in WINDOWS_DEVICE_NAMES:
            return False
    return True


def has_parent_os_path_segment_model(os_path: str) -> bool:
    return any(segment == ".." for segment in os_path.replace("\\", "/").split("/"))


def validate_behavior_model() -> None:
    accepted = (
        "openq4.cfg",
        "savegames/Quicksave0.save",
        "screenshots/shot00001.tga",
        "generated/rendermodels/maps/game/test.bmd5mesh",
        "logs/openq4.log",
        ".cache/generated.bin",
        "savegames/Player 1.save",
        "version..cfg",
        "devices/com10.cfg",
        "devices/lpt0.cfg",
        "devices/console.txt",
        "devices/auxiliary.txt",
    )
    rejected = (
        None,
        "",
        "/outside.cfg",
        "\\outside.cfg",
        "C:/outside.cfg",
        "C:outside.cfg",
        "../outside.cfg",
        "folder/../outside.cfg",
        "./inside.cfg",
        "folder/./inside.cfg",
        "folder//inside.cfg",
        "folder/",
        "folder\\inside.cfg",
        "folder\ninside.cfg",
        "folder\tinside.cfg",
        "folder/<inside>.cfg",
        'folder/"inside".cfg',
        "folder/inside?.cfg",
        "folder/inside*.cfg",
        "folder/inside|stream.cfg",
        "folder./inside.cfg",
        " folder/inside.cfg",
        "folder/ inside.cfg",
        "folder /inside.cfg",
        "inside.cfg.",
        "inside.cfg ",
        "con",
        "CON.txt",
        "folder/prn.cfg",
        "folder/AUX.data",
        "folder/nul.tar.gz",
        "folder/com1.cfg",
        "folder/COM9",
        "folder/lpt1.cfg",
        "folder/LPT9",
        "folder/com¹.cfg",
        "folder/COM².cfg",
        "folder/lpt³.cfg",
    )

    for path in accepted:
        if not is_safe_relative_write_path_model(path):
            raise AssertionError(f"Expected accepted relative mutation qpath: {path!r}")
    for path in rejected:
        if is_safe_relative_write_path_model(path):
            raise AssertionError(f"Expected rejected relative mutation qpath: {path!r}")

    accepted_os_paths = (
        "/home/player/openQ4.../baseoq4/openQ4Config.cfg",
        "/mnt/games/build_(openQ4,_Linux,_GOG_Assets...)/userdata/player/openQ4Config.cfg",
        r"C:\Games\openQ4...\baseoq4\openQ4Config.cfg",
        r"C:\Games\version..candidate\baseoq4\openQ4Config.cfg",
    )
    rejected_os_paths = (
        "../outside.cfg",
        "/home/player/../outside.cfg",
        r"C:\Games\..\outside.cfg",
        r"\\server\share\folder\..\outside.cfg",
        "/home/player/..",
    )
    for path in accepted_os_paths:
        if has_parent_os_path_segment_model(path):
            raise AssertionError(f"Expected accepted explicit OS path: {path!r}")
    for path in rejected_os_paths:
        if not has_parent_os_path_segment_model(path):
            raise AssertionError(f"Expected rejected explicit OS path: {path!r}")


def validate_generated_loadscreen_path_budget() -> None:
    final_qpath = "guis/assets/generated/loadscreens/airdefense1_1820x1024.tga"
    save_root = "x" * 178
    final_path = f"{save_root}/{final_qpath}"
    nonce = "0" * 32
    sibling_stage = f"{save_root}/{final_qpath.rsplit('/', 1)[0]}/_oq4.{nonce}.00000001.tmp"
    fixed_stage_qpath = f"_oq4/{nonce}.tmp"
    fixed_stage = f"{save_root}/{fixed_stage_qpath}"
    minimum_final_qpath = "guis/assets/generated/loadscreens/a_1x1.tga"

    if len(final_path) != 238 or len(sibling_stage) != 263:
        raise AssertionError("loadscreen path-budget fixture no longer models the Windows boundary")
    if len(fixed_stage) != 220 or len(fixed_stage) >= len(final_path):
        raise AssertionError("root-level loadscreen staging path does not preserve MAX_PATH headroom")
    if len(fixed_stage_qpath) >= len(minimum_final_qpath):
        raise AssertionError("loadscreen staging qpath is not shorter than every generated final qpath")
    if not is_safe_relative_write_path_model(fixed_stage_qpath):
        raise AssertionError("root-level loadscreen staging path is not a portable mutation qpath")


def validate_generated_binary_image_cache_fallback() -> None:
    def compact_digest(logical_name: str) -> str:
        normalized_name = logical_name.replace("\\", "/").lower()
        return hashlib.sha256(normalized_name.encode("utf-8")).hexdigest()[:32]

    logical_name = "makeIntensity( gfx/lights/squarelight1a)#__0500"
    digest = compact_digest(logical_name)
    legacy_qpath = (
        "generated/images/_programs/"
        "makeintensity_gfx_lights_squarelight1a_0500_c92dc23b.bimage"
    )
    compact_qpath = f"generated/images/_compact/v1/{digest}.bimage"
    save_root_length = 174

    if digest != "46731237362ef058d9144a9884e3e2e9":
        raise AssertionError("compact binary-image identity is not deterministic")
    if compact_digest("textures/a b#__0200") == compact_digest("textures/ab#__0200"):
        raise AssertionError("compact binary-image normalization erases logical-name identity")
    authored_digest = compact_digest(
        "gfx/effects/fluids_drips/brown_bubble_half#__0200#q4authored.tga"
    )
    fallback_digest = compact_digest(
        "gfx/effects/fluids_drips/brown_bubble_half#__0200#q4stockfallback.tga"
    )
    if authored_digest == fallback_digest:
        raise AssertionError("compact image cache aliases authored and stock-fallback identities")
    if save_root_length + 1 + len(legacy_qpath) != 261:
        raise AssertionError("binary-image path-budget fixture no longer crosses MAX_PATH")
    if save_root_length + 1 + len(compact_qpath) != 243:
        raise AssertionError("compact binary-image cache path lost its fixed short budget")
    if not is_safe_relative_write_path_model(compact_qpath):
        raise AssertionError("compact binary-image cache path is not a portable mutation qpath")

    source = read("src/imagetools/BinaryImage.cpp")
    image_header = read("src/imagetools/BinaryImage.h")
    image_load_source = read("src/renderer/Image_load.cpp")
    compact_name = function_body(source, "static void R_MakeCompactBinaryImageFileName(")
    write_cache = function_body(source, "ID_TIME_T idBinaryImage::WriteGeneratedFile(")
    checked_read = function_body(source, "ID_TIME_T idBinaryImage::LoadFromGeneratedFile( ID_TIME_T sourceFileTime )")
    unchecked_read = function_body(source, "ID_TIME_T idBinaryImage::LoadFromGeneratedFileUnchecked(")
    compact_unchecked_read = function_body(
        source, "ID_TIME_T idBinaryImage::LoadFromCompactGeneratedFileUnchecked("
    )
    load_image = function_body(image_load_source, "void idImage::ActuallyLoadImage(")

    for token in (
        "normalizedName.BackSlashesToSlashes();",
        "normalizedName.ToLower();",
        "idCrypto::SHA256(",
        "i < 16",
        '"generated/images/_compact/v1/%s.bimage"',
    ):
        require(compact_name, token, "versioned SHA-256/128 compact binary-image identity")
    reject(compact_name, 'normalizedName.Replace( " ", "" );',
           "compact binary-image identity must preserve meaningful spaces")

    require_order(write_cache,
                  'fileSystem->OpenFileWrite( writeFileName, "fs_cachepath" )',
                  "R_MakeCompactBinaryImageFileName( writeFileName, GetName() )",
                  "legacy binary-image write before compact fallback")
    require_order(write_cache,
                  "R_MakeCompactBinaryImageFileName( writeFileName, GetName() )",
                  '\t\toutputFile = fileSystem->OpenFileWrite( writeFileName, "fs_cachepath" );',
                  "compact binary-image fallback write")
    require_order(write_cache,
                  '\t\toutputFile = fileSystem->OpenFileWrite( writeFileName, "fs_cachepath" );',
                  "idLib::Warning(",
                  "binary-image warning only after compact fallback failure")
    require(write_cache, "return FILE_NOT_FOUND_TIMESTAMP;",
            "recoverable binary-image cache publication failure")

    require_order(checked_read,
                  "fileSystem->OpenFileRead( binaryFileName )",
                  "R_MakeCompactBinaryImageFileName( compactFileName, GetName() )",
                  "timestamp-checked binary-image legacy-first lookup")
    require_order(checked_read,
                  "R_MakeCompactBinaryImageFileName( compactFileName, GetName() )",
                  "fileSystem->OpenFileRead( compactFileName )",
                  "timestamp-checked binary-image compact fallback lookup")
    require(checked_read, "LoadFromGeneratedFile( compactFile",
            "timestamp-checked compact binary-image payload validation")
    reject(unchecked_read, "R_MakeCompactBinaryImageFileName(",
           "ordinary unchecked lookup must expose legacy rejection to its caller")
    require(compact_unchecked_read, "fileSystem->OpenFileRead( compactFileName )",
            "pure-policy-preserving compact unchecked lookup")
    reject(compact_unchecked_read, "OpenExplicitFileRead",
           "compact binary-image lookup must not bypass pure VFS policy")
    require(compact_unchecked_read, "LoadFromGeneratedFile( compactFile",
            "compact unchecked payload validation")
    require(image_header, "LoadFromCompactGeneratedFileUnchecked();",
            "compact unchecked retry API")

    accept_generated = function_body(load_image, "auto acceptGeneratedImage =")
    for token in (
        "im.GetFileHeader().sourceFileTime != sourceFileTime",
        "R_BinaryImageHeaderSupportedByRenderer( im.GetFileHeader() )",
        "R_GeneratedImageHeaderMatchesDerivedOpts( im.GetFileHeader(), opts, usage )",
    ):
        require(accept_generated, token, "shared legacy/compact cache acceptance")
    require_order(load_image,
                  "generatedImageAccepted = acceptGeneratedImage( binaryFileTime );",
                  "im.LoadFromCompactGeneratedFileUnchecked();",
                  "caller-rejected legacy before compact cache retry")
    require_order(load_image,
                  "im.LoadFromCompactGeneratedFileUnchecked();",
                  "\t\tgeneratedImageAccepted = acceptGeneratedImage( binaryFileTime );",
                  "compact cache receives full caller-level validation")
    require_order(load_image,
                  "im.LoadFromCompactGeneratedFileUnchecked();",
                  "if ( generatedImageAccepted ) {",
                  "compact cache retry before source decode")


def validate_generated_binary_image_write_integrity() -> None:
    source = read("src/imagetools/BinaryImage.cpp")
    image_load_source = read("src/renderer/Image_load.cpp")
    write_image = function_body(source, "bool idBinaryImage::WriteToFile(")
    write_cache = function_body(source, "ID_TIME_T idBinaryImage::WriteGeneratedFile(")
    load_image = function_body(image_load_source, "void idImage::ActuallyLoadImage(")

    metadata_fields = (
        "fileData.sourceFileTime",
        "fileData.headerMagic",
        "fileData.textureType",
        "fileData.format",
        "fileData.colorFormat",
        "fileData.width",
        "fileData.height",
        "fileData.numLevels",
        "img.level",
        "img.destZ",
        "img.width",
        "img.height",
        "img.dataSize",
    )
    for field in metadata_fields:
        require(
            write_image,
            f"file->WriteBig( {field} ) != sizeof( {field} )",
            f"binary-image short write for {field}",
        )
    if write_image.count("file->WriteBig(") != len(metadata_fields):
        raise AssertionError("binary-image metadata contains an unchecked WriteBig call")
    require(
        write_image,
        "file->WriteBig( fileData.numLevels ) != sizeof( fileData.numLevels ) ) {\n\t\treturn false;\n\t}",
        "binary-image file-header short-write rejection",
    )
    require(
        write_image,
        "file->WriteBig( img.dataSize ) != sizeof( img.dataSize ) ) {\n\t\t\treturn false;\n\t\t}",
        "binary-image mip-metadata short-write rejection",
    )
    require(
        write_image,
        "if ( file->Write( img.data, img.dataSize ) != img.dataSize ) {\n\t\t\treturn false;\n\t\t}",
        "binary-image mip payload short write",
    )

    require(
        write_cache,
        "if ( !WriteToFile( file, sourceFileTime ) ) {\n\t\treturn FILE_NOT_FOUND_TIMESTAMP;\n\t}",
        "generated binary-image write failure remains recoverable",
    )
    write_call = "binaryFileTime = im.WriteGeneratedFile( sourceFileTime );"
    write_call_index = load_image.find(write_call)
    if write_call_index == -1:
        raise AssertionError("image loader no longer publishes the generated binary image")
    post_write = load_image[write_call_index:]
    require_order(post_write, write_call, "AllocImage();",
                  "binary-image publication failure does not stop allocation")
    require_order(post_write, write_call, "SubImageUpload(",
                  "binary-image publication failure does not stop upload")
    allocation_index = post_write.find("AllocImage();")
    if "return" in post_write[len(write_call):allocation_index]:
        raise AssertionError("binary-image cache publication failure became fatal to image loading")


def validate_source_contract() -> None:
    source = read("src/framework/FileSystem.cpp")
    header = read("src/framework/FileSystem.h")

    device_helper = function_body(source, "static bool FS_IsWindowsDeviceQPathSegment(")
    parent_os_path_helper = function_body(source, "static bool FS_HasParentOSPathSegment(")
    validator = function_body(source, "bool FS_ValidateRelativeWritePath(")
    open_write = function_body(source, "idFile *idFileSystemLocal::OpenFileWrite(")
    open_append = function_body(source, "idFile *idFileSystemLocal::OpenFileAppend(")
    remove_file = function_body(source, "void idFileSystemLocal::RemoveFile(")
    remove_file_checked = function_body(source, "bool idFileSystemLocal::RemoveFileChecked(")
    promote_file = function_body(source, "bool idFileSystemLocal::PromoteFile(")
    write_file = function_body(source, "int idFileSystemLocal::WriteFile(")
    open_by_mode = function_body(source, "idFile *idFileSystemLocal::OpenFileByMode(")
    explicit_write = function_body(source, "idFile *idFileSystemLocal::OpenExplicitFileWrite(")
    explicit_remove = function_body(source, "int idFileSystemLocal::RemoveExplicitFile(")
    create_os_path = function_body(source, "void idFileSystemLocal::CreateOSPath(")

    require(validator, "relativePath == NULL || relativePath[ 0 ] == '\\0'", "empty mutation qpath rejection")
    require(validator, "relativePath[ 0 ] == '/' || relativePath[ 0 ] == '\\\\'", "rooted mutation qpath rejection")
    require(validator, "c == '\\\\' || c == ':'", "OS separator and volume marker rejection")
    require(validator, "c < 32", "control-character rejection")
    for character in ("<", ">", '"', "|", "?", "*"):
        require(validator, f"c == '{character}'", "Windows punctuation rejection")
    require(validator, "segmentLength == 0", "empty qpath segment rejection")
    require(validator, "segmentStart[ 0 ] == '.'", "dot qpath segment rejection")
    require(validator, "segmentStart[ 1 ] == '.'", "parent qpath segment rejection")
    require(validator, "segmentStart[ 0 ] == ' '", "leading-space rejection")
    require(validator, "segmentStart[ segmentLength - 1 ] == '.'", "trailing-dot rejection")
    require(validator, "segmentStart[ segmentLength - 1 ] == ' '", "trailing-space rejection")
    require(validator, "FS_IsWindowsDeviceQPathSegment( segmentStart, segmentLength )", "device-name rejection")

    for device_name in ("con", "prn", "aux", "nul", "com", "lpt"):
        require(device_helper, f'"{device_name}"', "Windows device-name rejection")
    require(device_helper, "digit >= '1' && digit <= '9'", "numbered Windows device-name rejection")
    require(device_helper, "digit == 0xB9", "superscript Windows device-name rejection")
    require(device_helper, "digit == 0xC2", "UTF-8 superscript Windows device-name rejection")

    require(parent_os_path_helper, "c != '/' && c != '\\\\'", "OS-path separator recognition")
    require(parent_os_path_helper, "scan - segmentStart == 2", "exact parent-component length")
    require(parent_os_path_helper, "segmentStart[ 0 ] == '.'", "parent-component first dot")
    require(parent_os_path_helper, "segmentStart[ 1 ] == '.'", "parent-component second dot")
    require(create_os_path, "FS_HasParentOSPathSegment( OSPath )", "explicit OS-path traversal rejection")
    require(create_os_path, 'strstr( OSPath, "::" )', "legacy parent-path syntax rejection")
    reject(create_os_path, 'strstr( OSPath, ".." )', "ordinary repeated dots remain valid")

    for body, context, first_mutation in (
        (write_file, "whole-file write API", "idFileSystemLocal::OpenFileWrite("),
        (open_write, "relative write API", "BuildOSPath("),
        (open_append, "relative append API", "BuildOSPath("),
        (remove_file, "relative remove API", "BuildOSPath("),
        (remove_file_checked, "checked relative remove API", "BuildOSPath("),
    ):
        require(body, "FS_ValidateRelativeWritePath( relativePath, &invalidReason )", context)
        require(body, "refusing unsafe relative path", context)
        require_order(body, "FS_ValidateRelativeWritePath(", first_mutation, context)

    require(write_file, "idFileSystemLocal::OpenFileWrite( relativePath, basePath )", "whole-file write funnel")
    require(open_by_mode, "OpenFileWrite( relativePath )", "mode write funnel")
    require(open_by_mode, "OpenFileAppend( relativePath, true )", "mode append funnel")

    require(remove_file_checked, "removalError == ENOENT",
            "checked cleanup treats an absent file as complete")
    require(remove_file_checked, "BuildOSPath( root, gameFolder, relativePath )",
            "checked cleanup resolves exactly one selected root")
    reject(remove_file_checked, "fs_cdpath", "checked cleanup does not walk overlay roots")

    for token in ("SDL_RenamePath", "MoveFileExA", "rename("):
        require(promote_file, token, "cross-platform atomic promotion")
    for token in ("CopyFile", "WriteFile(", "OpenFileWrite("):
        reject(promote_file, token, "atomic promotion has no copy fallback")

    for body, context in (
        (explicit_write, "explicit OS-path write API"),
        (explicit_remove, "explicit OS-path remove API"),
        (create_os_path, "explicit OS-path directory API"),
    ):
        reject(body, "FS_ValidateRelativeWritePath", context)

    require(header, "Relative mutation paths must be non-empty portable qpaths", "filesystem API contract")
    require(header, "Use the Explicit/OSPath APIs", "explicit OS-path API contract")
    require(header, "virtual bool\t\t\tRemoveFileChecked(", "checked relative cleanup API")
    require(header, "Returns true when the file was removed or was already absent.",
            "checked cleanup missing-file semantics")


def validate_generated_loadscreen_publication() -> None:
    session = read("src/framework/Session.cpp")
    image_files = read("src/imagetools/Image_files.cpp")
    image_header = read("src/renderer/Image.h")
    image_tools_header = read("src/imagetools/ImageTools.h")
    prepare = function_body(
        session,
        "static bool Session_PrepareExpandedLoadingBackground(",
    )
    secure_staging_failure = function_body(
        prepare,
        "if ( !Sys_GetSecureRandomBytes( stagingNonce, sizeof( stagingNonce ) ) )",
    )
    writer = function_body(image_files, "bool R_WriteTGA(")

    require(writer, "return fileSystem->WriteFile( filename, buffer, bufferSize, basePath ) == bufferSize;",
            "TGA writer reports short writes")
    require(image_header, "bool\tR_WriteTGA(", "renderer TGA writer result contract")
    require(image_tools_header, "bool\tR_WriteTGA(", "imagetools TGA writer result contract")
    require(prepare, "uint64 stagingNonce[2] = { 0, 0 };",
            "generated loadscreen 128-bit staging nonce")
    require(prepare, "Sys_GetSecureRandomBytes( stagingNonce, sizeof( stagingNonce ) )",
            "generated loadscreen cross-process CSPRNG token")
    require(secure_staging_failure, "Could not create a secure expanded-loadscreen staging path",
            "generated loadscreen no-CSPRNG source fallback")
    require(secure_staging_failure, "return false;",
            "generated loadscreen no-CSPRNG early return")
    require(prepare, 'const idStr stagingPath = va( "_oq4/%016llx%016llx.tmp"',
            "generated loadscreen short root-level staging path")
    reject(prepare, "static uint32 stagingSequence",
           "generated loadscreen CSPRNG identity needs no shared sequence")
    reject(prepare, "stagingPath.StripFilename();",
           "generated loadscreen staging must not inherit the long final directory")
    reject(prepare, 'stagingPath += va( "/_oq4.',
           "generated loadscreen staging must not remain beside the final file")
    reject(prepare, 'va( "%s.%016llx%016llx.%u.partial"',
           "generated loadscreen must not inflate the final path past platform limits")
    require(prepare, "R_WriteTGA( stagingPath.c_str()", "generated loadscreen staged write")
    require(prepare, "fileSystem->PromoteFile( stagingPath.c_str(), generatedPath.c_str()",
            "generated loadscreen atomic publication")
    require(prepare, "else if ( !Session_FileExistsInSearchPaths( generatedPath.c_str() ) )",
            "generated loadscreen active-VFS visibility gate")
    require(prepare, "declManager->FindMaterial( generatedPath.c_str() )",
            "generated loadscreen material preflight")
    require(prepare, "generatedMaterial->TestMaterialFlag( MF_DEFAULTED )",
            "generated loadscreen default-material rejection")
    require(prepare, "using the source levelshot",
            "generated loadscreen unavailable-path source fallback")
    require(prepare, "fileSystem->RemoveFileChecked( stagingPath.c_str()",
            "generated loadscreen failed-stage cleanup")
    require(prepare, "return published;", "generated loadscreen failure falls back to source")
    require_order(prepare, "R_WriteTGA( stagingPath.c_str()", "fileSystem->PromoteFile(",
                  "generated loadscreen write-before-publish order")
    require_order(prepare, "fileSystem->PromoteFile(",
                  "Session_FileExistsInSearchPaths( generatedPath.c_str() )",
                  "generated loadscreen publish-before-VFS-validation order")
    require_order(prepare, "Session_FileExistsInSearchPaths( generatedPath.c_str() )",
                  "declManager->FindMaterial( generatedPath.c_str() )",
                  "generated loadscreen VFS-before-material-validation order")
    require_order(prepare, "Sys_GetSecureRandomBytes( stagingNonce, sizeof( stagingNonce ) )",
                  'const idStr stagingPath = va( "_oq4/%016llx%016llx.tmp"',
                  "generated loadscreen secure-token-before-path order")
    require_order(prepare, "Sys_GetSecureRandomBytes( stagingNonce, sizeof( stagingNonce ) )",
                  "R_WriteTGA( stagingPath.c_str()",
                  "generated loadscreen secure-token-before-write order")
    reject(prepare, "R_WriteTGA( generatedPath.c_str()",
           "generated loadscreen must never truncate the live file in place")
    reject(prepare, "Sys_GetClockTicks()",
           "generated loadscreen staging token must not use a cross-process clock fallback")


def main() -> int:
    validate_behavior_model()
    validate_generated_loadscreen_path_budget()
    validate_generated_binary_image_cache_fallback()
    validate_generated_binary_image_write_integrity()
    validate_source_contract()
    validate_generated_loadscreen_publication()
    print("filesystem relative mutation qpath safety checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
