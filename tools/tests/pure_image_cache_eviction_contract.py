#!/usr/bin/env python3
"""Pin how a pure lock treats the generated image cache.

openQ4 writes derived images to <fs_cachepath>/<gameDir>/generated/images/.
An entry's header names its source by a stamp built from the pak member's
CRC-32 and length. Anyone can read those from the pak and copy them into a
forged entry with edited pixels, so the cache cannot be trusted on a pure
server. Two rules follow:

* While pure, the directory gate in OpenFileReadSearch refuses every cache
  read: FileAllowedFromDir never admits a .bimage.
* A cache read before the lock does not force a full engine restart. It sets
  loadedImageCacheFromDir instead of loadedFileFromDir. SetPureServerChecksums
  then leaves an eviction pending, and the client reloads every image whose
  resident pixels came from the cache once the lock is in place. If that
  fails, the client restarts as before. Any other directory read still
  restarts.

This test compiles the real path classifier and pins each link of that chain,
so a later change cannot drop the eviction or widen the exemption unnoticed.
The decision and its runtime evidence are recorded in
docs/dev/mp-network-qualification.md.
"""
from __future__ import annotations

from pathlib import Path
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


def require_order(source: str, first: str, second: str, message: str) -> None:
    require(source, first, message)
    require(source, second, message)
    if source.index(first) > source.index(second):
        raise AssertionError(f"{message}: {first!r} must precede {second!r}")


PROGRAM = r'''
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstddef>

struct idStr {
	static int Icmp( const char *a, const char *b ) {
		for ( ;; ++a, ++b ) {
			const int x = std::tolower( (unsigned char)*a ), y = std::tolower( (unsigned char)*b );
			if ( x != y ) { return x < y ? -1 : 1; }
			if ( !x ) { return 0; }
		}
	}
	static int Icmpn( const char *a, const char *b, int n ) {
		for ( ; n > 0; --n, ++a, ++b ) {
			const int x = std::tolower( (unsigned char)*a ), y = std::tolower( (unsigned char)*b );
			if ( x != y ) { return x < y ? -1 : 1; }
			if ( !x ) { return 0; }
		}
		return 0;
	}
};

/*CLASSIFIER*/

static int failures = 0;
static void Expect( const char *path, bool expected ) {
	if ( FS_IsGeneratedImageCachePath( path ) != expected ) {
		std::printf( "FAIL: %s should %sbe a generated image cache path\n", path ? path : "(null)", expected ? "" : "not " );
		++failures;
	}
}

int main() {
	Expect( "generated/images/textures/base/wall#__0200.bimage", true );
	Expect( "generated/images/_programs/makeintensity_gfx_lights_squarelight1a_0500_c92dc23b.bimage", true );
	Expect( "generated/images/_compact/v1/0123456789abcdef0123456789abcdef.bimage", true );
	Expect( "Generated/Images/Fonts/English/BIGCHARS#__0400M0.BIMAGE", true );
	// only image caches: other generated trees and every source keep the restart
	Expect( "generated/images/.bimage", false );
	Expect( "generated/images/textures/base/wall.tga", false );
	Expect( "generated/images/textures/base/wall.bimage.tga", false );
	Expect( "generated/imagesx/wall.bimage", false );
	Expect( "generated/sound/player/step.idwav", false );
	Expect( "generated/animations/models/marine.banim", false );
	Expect( "generated/vulkan/pipeline.cache", false );
	Expect( "xgenerated/images/wall.bimage", false );
	Expect( "maps/mp/q4dm1.map", false );
	Expect( "textures/base/wall.bimage", false );
	Expect( "", false );
	Expect( nullptr, false );
	if ( failures ) {
		return 1;
	}
	std::puts( "classifier: PASS" );
	return 0;
}
'''


def check_classifier(filesystem: str) -> None:
    classifier = function_body(filesystem, "static bool FS_IsGeneratedImageCachePath(")
    compiler = next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("a C++ compiler is required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pure-image-cache-", dir=ROOT / ".tmp") as directory:
        program, binary = Path(directory) / "classifier.cpp", Path(directory) / "classifier.exe"
        program.write_text(PROGRAM.replace("/*CLASSIFIER*/", classifier), encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(program), "-o", str(binary)],
                       check=True)
        subprocess.run([str(binary)], check=True)


def check_filesystem(filesystem: str) -> None:
    allowed = function_body(filesystem, "bool idFileSystemLocal::FileAllowedFromDir(")
    if "bimage" in allowed or "generated" in allowed:
        raise AssertionError("FileAllowedFromDir must not admit generated cache entries while pure")

    search = function_body(filesystem, "idFile *idFileSystemLocal::OpenFileReadSearch(")
    # the gate that keeps every cache entry unreadable while pure
    require_order(search, "if ( fs_restrict.GetBool() || serverPaks.Num() ) {",
                  "if ( !FileAllowedFromDir( relativePath ) ) {",
                  "the pure directory gate")
    require_order(search, "if ( !FileAllowedFromDir( relativePath ) ) {",
                  "netpath = BuildOSPath( dir->path, dir->gamedir, relativePath );",
                  "the pure gate runs before any directory open")
    # a cache read is classified first; everything else keeps the restart
    # (pure_expanded_loadscreen_contract.py owns the loading background half)
    require_order(search, "if ( FS_IsGeneratedImageCachePath( relativePath ) || FS_IsGeneratedLoadscreenPath( relativePath ) ) {",
                  "} else if ( !loadedFileFromDir && !FileAllowedFromDir( relativePath ) ) {",
                  "generated image cache reads are classified before the restart trigger")
    require(search, "loadedImageCacheFromDir = true;", "a cache read records a pending image reload")
    require(search, "loadedFileFromDir = true;", "other directory reads still force a restart")

    pure = function_body(filesystem, "fsPureReply_t idFileSystemLocal::SetPureServerChecksums(")
    require(pure, "if ( loadedFileFromDir ) {\n\t\tsuccess = false;", "a directory read still forces PURE_RESTART")
    require(pure, "if ( success && loadedImageCacheFromDir && serverPaks.Num() ) {\n\t\tpureImageCacheEvictionPending = true;",
            "the eviction is left pending only for a lock actually taken")
    take = function_body(filesystem, "bool idFileSystemLocal::TakePureImageCacheEviction(")
    require(take, "pureImageCacheEvictionPending = false;", "the eviction is handed on once")
    clear = function_body(filesystem, "void idFileSystemLocal::ClearPureChecksums(")
    require(clear, "pureImageCacheEvictionPending = false;", "clearing the lock drops the pending eviction")
    for reset in ("loadedImageCacheFromDir = false;", "pureImageCacheEvictionPending = false;"):
        if filesystem.count(reset) < 3:
            raise AssertionError(f"construction, shutdown and the hand-off must each reset {reset!r}")


def check_client(client: str) -> None:
    validate = function_body(client, "bool idAsyncClient::ValidatePureServerChecksums(")
    require_order(validate, "fsPureReply_t reply = fileSystem->SetPureServerChecksums(",
                  "if ( reply == PURE_OK && fileSystem->TakePureImageCacheEviction() ) {",
                  "the client asks for the eviction right after taking the lock")
    require_order(validate, "if ( reply == PURE_OK && fileSystem->TakePureImageCacheEviction() ) {",
                  "switch ( reply ) {", "the eviction runs before the reply is acted on")
    require(validate, "if ( renderSystem == NULL || !renderSystem->ReloadGeneratedCacheImages() ) {",
            "the client reloads the cached images")
    require(validate, "reply = PURE_RESTART;", "a failed reload falls back to the engine restart")
    require(validate, 'cmdSystem->BufferCommandText( CMD_EXEC_NOW, "reloadEngine" );', "the restart path remains")


def check_renderer(loader: str, manager: str, render_system: str, header: str, api: str) -> None:
    load = function_body(loader, "void idImage::ActuallyLoadImage(")
    require(load, "generatedCacheContent = consumedSource == ICS_GENERATED;", "an ordinary load records its provenance")
    require(load, "generatedCacheContent = source == ICS_GENERATED;", "a prepared load records its provenance")
    if load.count("generatedCacheContent = false;") < 4:
        raise AssertionError("generator, scratch and both default paths must clear the cache provenance")
    require(header, "bool		HoldsGeneratedCacheContent() const { return generatedCacheContent && IsLoaded(); }",
            "provenance only matters while a texture is resident")

    evict = function_body(manager, "bool idImageManager::ReloadGeneratedCacheImages(")
    require(evict, "if ( image == NULL || !image->HoldsGeneratedCacheContent() ) {", "only cache-sourced images reload")
    require_order(evict, "image->Reload( true );", "if ( image->HoldsGeneratedCacheContent() ) {",
                  "each image is checked after its forced reload")
    require(evict, "return failed == 0;", "any image left with cache pixels fails the eviction")
    require(render_system, "return globalImages->ReloadGeneratedCacheImages();", "the render system delegates")
    require(api, "#define RENDER_API_VERSION\t\t\t33", "the eviction slots are render API 33")


def main() -> None:
    filesystem = read("src/framework/FileSystem.cpp")
    check_classifier(filesystem)
    check_filesystem(filesystem)
    check_client(read("src/framework/async/AsyncClient.cpp"))
    check_renderer(read("src/renderer/Image_load.cpp"), read("src/renderer/ImageManager.cpp"),
                   read("src/renderer/RenderSystem.cpp"), read("src/renderer/Image.h"),
                   read("src/renderer/RenderModuleAPI.h"))
    print("pure_image_cache_eviction_contract: PASS (cache refused while pure, reloaded instead of a restart)")


if __name__ == "__main__":
    main()
