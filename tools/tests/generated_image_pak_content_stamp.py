#!/usr/bin/env python3
"""Pin the generated image cache's source identity for pk4 members.

The .bimage cache under <fs_savepath>/<gameDir>/generated/images/ keeps the
source identity it was derived from in its header and is reused only while the
current source reports the same value. Every pk4 member's Timestamp() is 0, so
when that value was the timestamp a texture changed inside a pak under the same
name kept loading the old cached image until generated/images was deleted.

The image pipeline therefore reads "content stamps": a loose file's
modification time, or for a pk4 member a value folded from its zip CRC-32 and
length below FILE_NOT_FOUND_TIMESTAMP. This test compiles the real stamp
helpers (for both 64-bit and 32-bit ID_TIME_T) and the renderer's two ordered
consumers, R_IsPreferredDDSStale and R_ImageSourceStampCurrent, and pins every
place a source stamp enters the image pipeline, so a new loader cannot quietly
go back to the timestamp.

The probe/full-open agreement inside the filesystem is exercised by
filesystem_length_probe_contract.py.
"""
from __future__ import annotations

from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing function {signature!r}")
    depth = 0
    for index in range(source.index("{", start), len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated body for {signature!r}")


def require(source: str, snippet: str, message: str) -> None:
    if snippet not in source:
        raise AssertionError(f"{message}: missing {snippet!r}")


PROGRAM = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <set>
typedef STAMP_TYPE ID_TIME_T;
static const ID_TIME_T FILE_NOT_FOUND_TIMESTAMP = static_cast<ID_TIME_T>( -1 );

struct idStr {
	const char *s;
	int IcmpPrefix( const char *prefix ) const {
		for ( int i = 0; prefix[i]; ++i ) {
			if ( s[i] != prefix[i] ) { return 1; }
		}
		return 0;
	}
};

STAMP_HELPERS

RENDERER_HELPERS

int main() {
	const ID_TIME_T a = FS_PakMemberContentStamp( 0x1234abcdu, 70000 );
	// a member's stamp is stable, never "not found", never a plausible file time
	assert( a == FS_PakMemberContentStamp( 0x1234abcdu, 70000 ) );
	assert( a != FILE_NOT_FOUND_TIMESTAMP && a < 0 && FS_IsPakMemberContentStamp( a ) );
	// changing the bytes (CRC) or the length under the same name changes it
	assert( FS_PakMemberContentStamp( 0x1234abceu, 70000 ) != a );
	assert( FS_PakMemberContentStamp( 0x1234abcdu, 70001 ) != a );
	// an empty member (CRC 0, length 0) still reads as a pak member, not a time
	assert( FS_IsPakMemberContentStamp( FS_PakMemberContentStamp( 0, 0 ) ) );
	assert( FS_IsPakMemberContentStamp( FS_PakMemberContentStamp( 0xffffffffu, 0x7fffffff ) ) );
	{
		std::set<ID_TIME_T> seen;
		for ( uint32_t crc = 0; crc < 4096; ++crc ) {
			const ID_TIME_T stamp = FS_PakMemberContentStamp( crc * 2654435761u, 1024 );
			assert( FS_IsPakMemberContentStamp( stamp ) );
			seen.insert( stamp );
		}
		assert( seen.size() == 4096 );
	}
	// loose times and "not found" are not member stamps
	assert( !FS_IsPakMemberContentStamp( 0 ) && !FS_IsPakMemberContentStamp( 1760000000 ) );
	assert( !FS_IsPakMemberContentStamp( FILE_NOT_FOUND_TIMESTAMP ) );
	// ordered checks see a member as the 0 every pak file always reported
	assert( FS_ContentStampOrderTime( a ) == 0 && FS_ContentStampOrderTime( 1760000000 ) == 1760000000 );

	// combining: 0 is empty, loose-only keeps the newest time
	assert( FS_CombineContentStamps( 0, a ) == a && FS_CombineContentStamps( a, 0 ) == a );
	assert( FS_CombineContentStamps( 0, 1700000000 ) == 1700000000 );
	assert( FS_CombineContentStamps( 1700000000, 1600000000 ) == 1700000000 );
	assert( FS_CombineContentStamps( 1600000000, 1700000000 ) == 1700000000 );
	{
		// with a member involved, a change to any part changes the result
		const ID_TIME_T b = FS_PakMemberContentStamp( 0x0badf00du, 4096 );
		const ID_TIME_T b2 = FS_PakMemberContentStamp( 0x0badf00eu, 4096 );
		const ID_TIME_T ab = FS_CombineContentStamps( FS_CombineContentStamps( 0, a ), b );
		assert( FS_IsPakMemberContentStamp( ab ) && ab != a && ab != b );
		assert( ab == FS_CombineContentStamps( FS_CombineContentStamps( 0, a ), b ) );
		assert( FS_CombineContentStamps( a, b2 ) != ab );
		assert( FS_CombineContentStamps( FS_PakMemberContentStamp( 0x1234abceu, 70000 ), b ) != ab );
		// mixed with a loose file: still a member stamp, and the loose time counts
		const ID_TIME_T mixed = FS_CombineContentStamps( a, 1700000000 );
		assert( FS_IsPakMemberContentStamp( mixed ) );
		assert( mixed != FS_CombineContentStamps( a, 1700000001 ) );
		assert( FS_IsPakMemberContentStamp( FS_CombineContentStamps( 1700000000, a ) ) );
	}

	// preferred-DDS staleness keeps its old answers for pak sources
	const idStr dds = { "textures/base/wall.dds" };
	const idStr progimg = { "progimg/textures/base/wall.dds" };
	const ID_TIME_T pakDDS = FS_PakMemberContentStamp( 0x11111111u, 2048 );
	const ID_TIME_T pakSource = FS_PakMemberContentStamp( 0x22222222u, 4096 );
	assert( !R_IsPreferredDDSStale( dds, pakDDS, pakSource ) );				// both in paks
	assert( !R_IsPreferredDDSStale( dds, pakDDS, FILE_NOT_FOUND_TIMESTAMP ) );
	assert( R_IsPreferredDDSStale( dds, pakDDS, 1700000000 ) );			// a loose override is newer
	assert( !R_IsPreferredDDSStale( dds, 1700000000, pakSource ) );		// a loose DDS beats a pak source
	assert( R_IsPreferredDDSStale( dds, 1600000000, 1700000000 ) );
	assert( !R_IsPreferredDDSStale( progimg, 1699999998, 1700000000 ) );	// the retail quanta allowance
	assert( R_IsPreferredDDSStale( progimg, 1699999990, 1700000000 ) );

	// reload: a changed member reloads, an unchanged one does not
	assert( R_ImageSourceStampCurrent( pakSource, pakSource ) );
	assert( !R_ImageSourceStampCurrent( FS_PakMemberContentStamp( 0x22222223u, 4096 ), pakSource ) );
	assert( !R_ImageSourceStampCurrent( 1700000000, pakSource ) );			// a loose override appeared
	assert( !R_ImageSourceStampCurrent( pakSource, 1700000000 ) );			// the loose override went away
	assert( !R_ImageSourceStampCurrent( pakSource, 0 ) );					// loaded before content stamps
	assert( R_ImageSourceStampCurrent( FILE_NOT_FOUND_TIMESTAMP, pakSource ) );
	assert( R_ImageSourceStampCurrent( 1600000000, 1700000000 ) && !R_ImageSourceStampCurrent( 1700000001, 1700000000 ) );

	printf( "content stamps hold for a %d-bit ID_TIME_T\n", static_cast<int>( sizeof( ID_TIME_T ) * 8 ) );
	return 0;
}
'''


def compile_and_run(stamp_type: str, stamp_helpers: str, renderer_helpers: str) -> None:
    compiler = next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("a C++ compiler is required")
    source = (PROGRAM.replace("STAMP_TYPE", stamp_type)
              .replace("STAMP_HELPERS", stamp_helpers)
              .replace("RENDERER_HELPERS", renderer_helpers))
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pak-content-stamp-", dir=ROOT / ".tmp") as directory:
        program, binary = Path(directory) / "stamp.cpp", Path(directory) / "stamp.exe"
        program.write_text(source, encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                        str(program), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


def stamp_helpers() -> str:
    header = read("src/framework/FileSystem.h")
    start = header.index("inline ID_TIME_T FS_FoldContentIdentity(")
    combine = function_body(header, "inline ID_TIME_T FS_CombineContentStamps(")
    return header[start : header.index(combine) + len(combine)]


def renderer_helpers() -> str:
    load = read("src/renderer/Image_load.cpp")
    return "\n".join((
        function_body(load, "static bool R_IsPreferredDDSStale("),
        function_body(load, "static bool R_ImageSourceStampCurrent("),
    ))


def check_filesystem() -> None:
    fs = read("src/framework/FileSystem.cpp")
    header = read("src/framework/FileSystem.h")
    require(header, "virtual int\t\t\t\tReadFileContentStamp( const char *relativePath, void **buffer, ID_TIME_T *contentStamp ) = 0;",
            "the filesystem interface must offer the content-stamp read")
    require(fs, "buildBuffer[i].crc = static_cast<uint32_t>( file_info.crc );",
            "the pak index must keep each member's central-directory CRC")
    zip_open = function_body(fs, "idFile_InZip * idFileSystemLocal::ReadFileFromZip(")
    require(zip_open, "file->contentCrc = pakFile->crc;",
            "a full open must stamp from the same index entry as the probe")
    if fs.count("probe->crc = pakFile->crc;") != 2:
        raise AssertionError("both pak and addon probes must report the member CRC")
    stamped = function_body(fs, "int idFileSystemLocal::ReadFileStamped(")
    require(stamped, "*timestamp = contentStamp ? FS_PakMemberContentStamp( probe.crc, probe.length ) : 0;",
            "the probe must answer the content stamp from the index")
    require(stamped, "*timestamp = contentStamp ? f->ContentStamp() : f->Timestamp();",
            "a full read must report the opened file's content stamp")
    # Timestamp() itself stays 0: decl reloads, model reloads, addon extraction
    # and the md5r/proc caches order it against loose times.
    zip_time = function_body(read("src/framework/File.cpp"), "ID_TIME_T idFile_InZip::Timestamp(")
    if re.sub(r"\s+", " ", zip_time).find("{ return 0; }") < 0:
        raise AssertionError("idFile_InZip::Timestamp must keep reporting 0")
    require(read("src/framework/File.cpp"), "return FS_PakMemberContentStamp( contentCrc, fileSize );",
            "a pk4 member's ContentStamp must come from its CRC and length")
    level_load = read("src/framework/LevelLoadCacheManager.cpp")
    require(level_load, "ID_TIME_T ContentStamp() override { return contentStamp; }",
            "a preloaded level-load source must forward the content stamp")
    require(level_load, "authoritativeSource->ContentStamp()",
            "a preloaded level-load source must take its stamp from the authoritative file")


def check_image_pipeline() -> None:
    files = read("src/imagetools/Image_files.cpp")
    for loader in ("static void LoadTGA( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp ) {",
                   "static void LoadDDS( const char *name, byte **pic, int *width, int *height, ID_TIME_T *timestamp, bool decodeRXGBNormalMap ) {"):
        body = function_body(files, loader)
        require(body, "R_ReadImageSourceFile( name, NULL, timestamp );", f"{loader[:20]} probe")
        require(body, "R_ReadImageSourceFile( name, (void **)&buffer, timestamp );", f"{loader[:20]} read")
        if "fileSystem->ReadFile(" in body:
            raise AssertionError(f"{loader[:20]} must not read its source stamp through ReadFile")
    # the tape helper (an image worker's recorded reads) is a content-stamp read too
    helper = function_body(files, "static int R_ReadImageSourceFile( const char *name, void **buffer, ID_TIME_T *timestamp ) {")
    require(helper, "return fileSystem->ReadFileContentStamp( name, buffer, timestamp );", "untaped source read")
    require(helper, "fileSystem->ReadFileContentStamp( name, &bytes, &stamp );", "recorded source read")
    require(function_body(files, "static void LoadJPG( const char *filename,"), "*timestamp = f->ContentStamp();", "LoadJPG")
    require(function_body(files, "static bool R_ReadDDSFileInfoUncached("), "*timestamp = file->ContentStamp();",
            "DDS header probe")
    require(function_body(files, "bool R_LoadPrecompressedDDS("), "*timestamp = ddsFile->ContentStamp();",
            "precompressed DDS load")
    require(function_body(files, "bool R_LoadCubeImages("),
            "*timestamp = FS_CombineContentStamps( *timestamp, thisTime );", "cube faces")
    if re.search(r"->Timestamp\(\)", files):
        raise AssertionError("image sources must report ContentStamp(), not Timestamp()")
    program = read("src/imagetools/Image_program.cpp")
    require(program, "*timestamps = FS_CombineContentStamps(*timestamps, timestamp);", "image programs")
    if "timestamp > *timestamps" in program:
        raise AssertionError("image programs must not max-combine content stamps")
    require(read("src/renderer/ImageManager.cpp"), "entry.timestamp = file->ContentStamp();",
            "the level DDS prefetch must hand out content stamps")
    load = read("src/renderer/Image_load.cpp")
    reload = function_body(load, "void idImage::Reload(")
    require(reload, "R_ImageSourceStampCurrent( current, sourceFileTime )", "image reload")
    if "current <= sourceFileTime" in reload:
        raise AssertionError("image reload must not order content stamps")
    # the cache gate itself stays an equality test against the header
    require(load, "if ( im.GetFileHeader().sourceFileTime != sourceFileTime ) {", "generated cache gate")


def main() -> None:
    check_filesystem()
    check_image_pipeline()
    helpers, renderer = stamp_helpers(), renderer_helpers()
    compile_and_run("long long", helpers, renderer)
    compile_and_run("int", helpers, renderer)
    print("generated_image_pak_content_stamp: PASS (pk4 members re-derive on a CRC change)")


if __name__ == "__main__":
    main()
