#!/usr/bin/env python3
"""A light-grid bake releases each capture and builds on its own output.

Bake captures render through BeginFrame and run their back end inline, but never
reach EndFrame. Nothing released what each cube face allocated: frame memory grew
about 300 KB a face for the whole bake (3.5 GB on mp/q4dm8, and game/airdefense2
ran out of memory), frame-temp vertex data stayed live, and the upload stream never
fenced the buffer a later capture reuses. Every capture now ends with EndFrame's
release calls, without presenting.

openQ4 searches fs_savepath last, so a bake that read its atlases back by name found
any copy a map ships in pak1 first: re-baking such a map packed the shipped atlases
and lit bounces 2+ with the shipped grid. The bake now reads its own atlases and
metadata from the save path, uploads them in the format a by-name load would use,
and warns when another copy will load in place of the bake.
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LIGHTGRID = ROOT / "src" / "renderer" / "RenderWorld_lightgrid.cpp"
RENDER_SYSTEM = ROOT / "src" / "renderer" / "RenderSystem.cpp"
IMAGE_LOAD = ROOT / "src" / "renderer" / "Image_load.cpp"

HARNESS = r"""
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

typedef unsigned char byte;
typedef long long ID_TIME_T;
static const ID_TIME_T FILE_NOT_FOUND_TIMESTAMP = -1;
static const int LIGHTGRID_BAKE_RGB_BYTES_PER_PIXEL = 3;

template<class T> class idTempArray {
public:
	explicit idTempArray( size_t count ) : items( count ) {}
	T *Ptr() { return items.data(); }
private:
	std::vector<T> items;
};
static void *R_StaticAlloc( int bytes ) { return std::malloc( bytes ); }
static void R_StaticFree( void *ptr ) { std::free( ptr ); }

struct idFile {
	std::vector<byte> data;
	size_t pos = 0;
	int Write( const void *buffer, int len ) {
		const byte *bytes = static_cast<const byte *>( buffer );
		data.insert( data.end(), bytes, bytes + len );
		return len;
	}
	int Read( void *buffer, int len ) {
		const int got = static_cast<int>( std::min<size_t>( len, data.size() - pos ) );
		std::memcpy( buffer, data.data() + pos, got );
		pos += got;
		return got;
	}
	ID_TIME_T Timestamp() { return 1234; }
};

static idFile savedFile;
static bool savedExists = true;
static std::string requestedBase, requestedPath;
struct idFileSystemStub {
	const char *RelativePathToOSPath( const char *relativePath, const char *basePath ) {
		requestedBase = basePath;
		requestedPath = std::string( "save/" ) + relativePath;
		return requestedPath.c_str();
	}
	idFile *OpenExplicitFileRead( const char *osPath ) {
		if ( !savedExists || requestedPath != osPath ) {
			return nullptr;
		}
		idFile *file = new idFile( savedFile );
		file->pos = 0;
		return file;
	}
	void CloseFile( idFile *file ) { delete file; }
};
static idFileSystemStub fileSystemStub;
static idFileSystemStub *fileSystem = &fileSystemStub;

@WRITE_HEADER@
@WRITE_ROWS@
@READ_BAKED@

static int failures = 0;
#define EXPECT( cond ) do { if ( !( cond ) ) { std::printf( "line %d: %s\n", __LINE__, #cond ); failures++; } } while ( 0 )

static std::vector<byte> Pattern( int width, int height ) {
	std::vector<byte> rgb( width * height * 3 );
	for ( size_t i = 0; i < rgb.size(); i++ ) {
		rgb[i] = static_cast<byte>( i * 37 + 11 );
	}
	return rgb;
}

static void ExpectPixels( const byte *pic, const std::vector<byte> &rgb, int width, int height ) {
	for ( int i = 0; i < width * height; i++ ) {
		EXPECT( pic[ i * 4 + 0 ] == rgb[ i * 3 + 0 ] );
		EXPECT( pic[ i * 4 + 1 ] == rgb[ i * 3 + 1 ] );
		EXPECT( pic[ i * 4 + 2 ] == rgb[ i * 3 + 2 ] );
		EXPECT( pic[ i * 4 + 3 ] == 255 );
	}
}

int main() {
	const int width = 5, height = 3;
	const std::vector<byte> rgb = Pattern( width, height );

	// what the bake writes reads back exactly, from the save path
	savedFile = idFile();
	EXPECT( LightGrid_WriteTGA24Header( &savedFile, width, height ) );
	EXPECT( LightGrid_WriteRGBRowsAsTGA( &savedFile, rgb.data(), width, height ) );
	int w = 0, h = 0;
	ID_TIME_T stamp = 0;
	byte *pic = LightGrid_ReadBakedTGA( "env/maps/test/area0_lightgrid_amb.tga", w, h, stamp );
	EXPECT( pic != nullptr && w == width && h == height && stamp == 1234 );
	EXPECT( requestedBase == "fs_savepath" );
	if ( pic != nullptr ) {
		ExpectPixels( pic, rgb, width, height );
		R_StaticFree( pic );
	}

	// a bottom-up TGA comes back top-down
	idFile bottomUp;
	byte header[18] = {};
	header[2] = 2; header[12] = width; header[14] = height; header[16] = 24;
	bottomUp.Write( header, sizeof( header ) );
	for ( int y = height - 1; y >= 0; y-- ) {
		for ( int x = 0; x < width; x++ ) {
			const byte *p = &rgb[ ( y * width + x ) * 3 ];
			const byte bgr[3] = { p[2], p[1], p[0] };
			bottomUp.Write( bgr, 3 );
		}
	}
	savedFile = bottomUp;
	pic = LightGrid_ReadBakedTGA( "a.tga", w, h, stamp );
	EXPECT( pic != nullptr );
	if ( pic != nullptr ) {
		ExpectPixels( pic, rgb, width, height );
		R_StaticFree( pic );
	}

	// anything else is refused, with the not-found timestamp
	idFile compressed = bottomUp;
	compressed.data[2] = 10;
	idFile alpha = bottomUp;
	alpha.data[16] = 32;
	idFile truncated = bottomUp;
	truncated.data.resize( truncated.data.size() - 1 );
	idFile empty = bottomUp;
	empty.data[12] = 0;
	idFile huge = bottomUp;
	huge.data[12] = 0xff; huge.data[13] = 0xff;
	idFile wide;	// complete, but wider than any atlas the bake writes
	byte wideHeader[18] = {};
	wideHeader[2] = 2; wideHeader[12] = 16385 & 255; wideHeader[13] = 16385 >> 8; wideHeader[14] = 1; wideHeader[16] = 24;
	wide.Write( wideHeader, sizeof( wideHeader ) );
	wide.data.resize( wide.data.size() + 16385 * 3, 7 );
	const idFile bad[] = { compressed, alpha, truncated, empty, huge, wide };
	for ( const idFile &file : bad ) {
		savedFile = file;
		stamp = 0;
		EXPECT( LightGrid_ReadBakedTGA( "a.tga", w, h, stamp ) == nullptr && w == 0 && h == 0 && stamp == FILE_NOT_FOUND_TIMESTAMP );
	}
	savedExists = false;
	EXPECT( LightGrid_ReadBakedTGA( "a.tga", w, h, stamp ) == nullptr );
	return failures ? 1 : 0;
}
"""


def fail(message: str) -> None:
    raise AssertionError(message)


def block(source: str, signature: str) -> str:
    """The text of a function, from its signature to its closing brace; prototypes are skipped."""
    start = source.find(signature)
    while start >= 0:
        brace, semicolon = source.find("{", start), source.find(";", start)
        if brace >= 0 and (semicolon < 0 or brace < semicolon):
            break
        start = source.find(signature, start + len(signature))
    if start < 0:
        fail(f"missing {signature}")
    depth = 0
    for index in range(source.index("{", start), len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    fail(f"unbalanced braces after {signature}")
    return ""


def require_order(text: str, snippets: tuple[str, ...], context: str) -> None:
    positions = [text.find(snippet) for snippet in snippets]
    if -1 in positions or positions != sorted(positions):
        fail(f"{context}: expected {snippets!r} in this order")


RELEASE = ("R_ToggleSmpFrame();", "vertexCache.EndFrame();", "R_RendererUpload_EndFrame();")


def check_capture_release(lightgrid: str, render_system: str) -> None:
    finish = block(lightgrid, "static void LightGrid_FinishCapture()")
    require_order(finish, RELEASE, "a bake capture must release its frame as EndFrame does")
    if "RC_SWAP_BUFFERS" in finish or "GLimp_SwapBuffers" in finish:
        fail("a bake capture must not present")
    # keep the capture tail in step with EndFrame's own release calls, which
    # EndFrame runs through CompleteBackEndFrame (inline, or from the render
    # thread's sync when r_vkRenderThread hands it the frame)
    if "CompleteBackEndFrame();" not in block(render_system, "void idRenderSystemLocal::EndFrame("):
        fail("EndFrame must release its frame through CompleteBackEndFrame")
    require_order(block(render_system, "void idRenderSystemLocal::CompleteBackEndFrame("), RELEASE,
                  "EndFrame's release calls")

    scene = block(lightgrid, "static void LightGrid_RenderCaptureScene(")
    require_order(scene, ("tr.BeginFrame(", "RB_ExecuteBackEndCommands( frameData->cmdHead );", "LightGrid_FinishCapture();"),
                  "the async capture must release its frame after the back end runs")
    sync = block(lightgrid, "static void LightGrid_CaptureViewRGB(")
    reads = sync.count("R_ReadTiledPixels(")
    released = len(re.findall(r"R_ReadTiledPixels\( width, height, rgbBuffer\.Ptr\(\), ref \);\s*LightGrid_FinishCapture\(\);", sync))
    if reads == 0 or released != reads:
        fail("every synchronous capture must release its frame")


def check_reads_own_output(lightgrid: str, image_load: str) -> dict[str, str]:
    reader = block(lightgrid, "static byte *LightGrid_ReadBakedTGA(")
    if 'fileSystem->OpenExplicitFileRead( fileSystem->RelativePathToOSPath( name, "fs_savepath" ) )' not in reader:
        fail("the bake must read its atlases from the save path, not by name")
    pack = block(lightgrid, "static bool LightGrid_WritePackImagePayload(")
    if "LightGrid_ReadBakedTGA(" not in pack or "R_LoadImage(" in pack:
        fail("the pack writer must pack the atlases this bake wrote")

    bake = block(lightgrid, "bool R_BakeCurrentLightGrids(")
    if "LoadLightGridImages(" in bake:
        fail("a bake must not reload its atlases by name")
    if bake.count("LightGrid_UseBakedAtlases( world );") != 2:
        fail("bounces 2+ and the finished bake must use the atlases this bake wrote")
    require_order(bake, ('RelativePathToOSPath( stagedLightGridWrite.finalName.c_str(), "fs_savepath" )',
                         "world->LoadLightGridFile( savedLightGrid.c_str(), true )"),
                  "separateAreas must pack this bake's metadata")
    require_order(bake, ("wrotePack = LightGrid_WriteLightGridPackFile(", "LightGrid_BakeLoadsNextTime( packName.c_str() )"),
                  "a bake shadowed by another copy must say so")

    use = block(lightgrid, "static void LightGrid_UseBakedAtlases(")
    require_order(use, ("world->ReleaseLightGridPack();",
                        "LightGrid_ClearPackedImageRef( lightGrid.packedIrradianceImage );",
                        "LightGrid_ClearPackedImageRef( lightGrid.packedVisibilityImage );",
                        "LightGrid_ClearPackedImageRef( lightGrid.packedProbeImage );",
                        "LightGrid_UploadBakedAtlas("), "baked atlases replace a loaded pack")
    if "CountValidGridPoints() <= 0" not in use:
        fail("an area the bake wrote nothing for must not pick up an older or shipped atlas")
    upload = block(lightgrid, "static bool LightGrid_UploadBakedAtlas(")
    for token in ("LightGrid_ReadBakedTGA(", "LightGrid_PackChunkFormat( kind )", "image->AllocImage( opts, TF_LINEAR, TR_CLAMP );"):
        if token not in upload:
            fail(f"baked atlases must upload as a by-name load would ({token!r})")

    # the upload's format choice must be the one a by-name load derives
    formats = block(lightgrid, "static textureFormat_t LightGrid_PackChunkFormat(")
    if "return glConfig.textureCompressionAvailable ? FMT_DXT1 : FMT_RGB565;" not in formats:
        fail("irradiance atlases upload as DXT1, or RGB565 without S3TC")
    derive = block(image_load, "ID_INLINE void idImage::DeriveOpts()")
    for usage, expected in (("TD_LIGHTGRID:", "opts.format = glConfig.textureCompressionAvailable ? FMT_DXT1 : FMT_RGB565;"),
                            ("TD_LIGHTGRID_VISIBILITY:", "opts.format = FMT_RGBA8;"),
                            ("TD_LIGHTGRID_PROBE:", "opts.format = FMT_RGBA8;")):
        case = derive[derive.index(f"case {usage}"):]
        if expected not in case[:case.index("break;")]:
            fail(f"a by-name load of {usage[:-1]} no longer derives {expected!r}; keep LightGrid_PackChunkFormat in step")

    return {
        "@WRITE_HEADER@": block(lightgrid, "static bool LightGrid_WriteTGA24Header("),
        "@WRITE_ROWS@": block(lightgrid, "static bool LightGrid_WriteRGBRowsAsTGA("),
        "@READ_BAKED@": reader,
    }


def run_harness(parts: dict[str, str]) -> None:
    compiler = shutil.which("clang++") or shutil.which("g++") or shutil.which("c++")
    if compiler is None:
        fail("no C++ compiler found for the baked-atlas harness")
    source = HARNESS
    for marker, text in parts.items():
        source = source.replace(marker, text)
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="lightgrid-bake-", dir=ROOT / ".tmp") as work:
        cpp = Path(work) / "baked_atlas.cpp"
        exe = Path(work) / ("baked_atlas.exe" if sys.platform == "win32" else "baked_atlas")
        cpp.write_text(source, encoding="utf-8")
        built = subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(exe)], capture_output=True, text=True)
        if built.returncode:
            fail(f"baked-atlas harness did not compile:\n{built.stdout}{built.stderr}")
        ran = subprocess.run([str(exe)], capture_output=True, text=True)
        if ran.returncode:
            fail(f"the bake does not read back what it writes:\n{ran.stdout}{ran.stderr}")


def main() -> int:
    lightgrid = LIGHTGRID.read_text(encoding="utf-8").replace("\r\n", "\n")
    render_system = RENDER_SYSTEM.read_text(encoding="utf-8").replace("\r\n", "\n")
    image_load = IMAGE_LOAD.read_text(encoding="utf-8").replace("\r\n", "\n")
    check_capture_release(lightgrid, render_system)
    run_harness(check_reads_own_output(lightgrid, image_load))
    print("lightgrid_bake_contract: ok")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as error:
        print(f"lightgrid_bake_contract: FAIL: {error}", file=sys.stderr)
        sys.exit(1)
