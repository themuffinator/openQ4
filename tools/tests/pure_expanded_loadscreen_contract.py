#!/usr/bin/env python3
"""Pin how a pure lock treats the session's expanded loading backgrounds.

In a window that is not 4:3 the session composes the map's levelshot, with any
_left/_right/_top/_bottom art, into one image of the window's shape. It
publishes that image under fs_savepath as
guis/assets/generated/loadscreens/<map>_<w>x<h>.tga and loads it back through
the search paths. The image is openQ4's own derived data, and like the
generated image cache it is never read while pure. Until 9 November 2026 a
client that had loaded any map in such a window counted that read as a file
loaded from a directory, so its first pure connect ran a full ReloadEngine.
The rules decided then, recorded in docs/dev/mp-network-qualification.md:

* While a pure lock or fs_restrict holds, the session composes nothing
  (FS_GeneratedDataReadsRefused) and shows the stock levelshot. The directory
  gate in OpenFileReadSearch would refuse the file anyway, and the session
  still requires the active VFS to expose whatever it published.
* A read before the lock sets loadedImageCacheFromDir, as a generated image
  cache read does, not loadedFileFromDir. SetPureServerChecksums leaves an
  eviction pending, and the client's ReloadGeneratedCacheImages purges every
  resident loading background; a purge that fails restarts the engine as
  before. Every other directory read still restarts.
* Only the names the session writes are exempt from the restart: a single
  segment ending in _<width>x<height>.tga. The renderer purges every image
  under the folder, which covers that set.

This test compiles the real filesystem classifier and the renderer's name test
against every name shape the session's own format string produces and against
near misses, then pins each link in the production source. --mutations breaks
each link and requires the check that owns it to fail.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

SOURCES = {
    "fs": "src/framework/FileSystem.cpp",
    "fs_h": "src/framework/FileSystem.h",
    "session": "src/framework/Session.cpp",
    "client": "src/framework/async/AsyncClient.cpp",
    "manager": "src/renderer/ImageManager.cpp",
    "render_system": "src/renderer/RenderSystem.cpp",
}

TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
PREFIX = re.compile(r'static const char prefix\[\] = "([^"]*)";')


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8").replace("\r\n", "\n")


def braced(source: str, signature: str) -> str:
    """Return source from signature through its matching brace, skipping strings and comments."""
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing {signature!r}")
    level = 0
    for match in TOKEN.finditer(source, source.index("{", start)):
        if match.group() == "{":
            level += 1
        elif match.group() == "}":
            level -= 1
            if level == 0:
                return source[start : match.end()]
    raise AssertionError(f"unterminated body for {signature!r}")


def require(source: str, snippet: str, message: str) -> None:
    if snippet not in source:
        raise AssertionError(f"{message}: missing {snippet!r}")


def forbid(source: str, snippet: str, message: str) -> None:
    if snippet in source:
        raise AssertionError(f"{message}: found {snippet!r}")


def require_order(source: str, first: str, second: str, message: str) -> None:
    require(source, first, message)
    require(source, second, message)
    if source.index(first) > source.index(second):
        raise AssertionError(f"{message}: {first!r} must precede {second!r}")


def prefix_of(body: str, what: str) -> str:
    match = PREFIX.search(body)
    if match is None:
        raise AssertionError(f"{what} no longer names its folder prefix")
    return match.group(1)


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

/*FS_CLASSIFIER*/

/*RENDERER_NAME_TEST*/

static int failures = 0;
static void Expect( const char *path, bool expected ) {
	if ( FS_IsGeneratedLoadscreenPath( path ) != expected ) {
		std::printf( "FAIL: %s should %sbe exempt as an expanded loading background\n", path ? path : "(null)", expected ? "" : "not " );
		++failures;
	}
	// every exempt read must be evicted at the lock, with or without the
	// extension the image name may drop
	if ( expected ) {
		const size_t length = std::strlen( path ) - 4;
		char stem[ 512 ] = {};
		std::memcpy( stem, path, length < sizeof( stem ) - 1 ? length : sizeof( stem ) - 1 );
		if ( !R_IsGeneratedLoadscreenImage( path ) || !R_IsGeneratedLoadscreenImage( stem ) ) {
			std::printf( "FAIL: the renderer would leave %s resident after the lock\n", path );
			++failures;
		}
	}
}

int main() {
	// what the session writes: Session_MakeExpandedLoadingBackgroundPath
/*SESSION_PATHS*/
	Expect( "GUIS/Assets/Generated/LoadScreens/Q4DM1_1820X1024.TGA", true );
	// near misses keep the restart
	Expect( "guis/assets/generated/loadscreens/q4dm1.tga", false );
	Expect( "guis/assets/generated/loadscreens/q4dm1_1820x.tga", false );
	Expect( "guis/assets/generated/loadscreens/q4dm1_x1024.tga", false );
	Expect( "guis/assets/generated/loadscreens/q4dm1_1820-1024.tga", false );
	Expect( "guis/assets/generated/loadscreens/q4dm1-1820x1024.tga", false );
	Expect( "guis/assets/generated/loadscreens/_1820x1024.tga", false );
	Expect( "guis/assets/generated/loadscreens/1820x1024.tga", false );
	Expect( "guis/assets/generated/loadscreens/mp/q4dm1_1820x1024.tga", false );
	Expect( "guis/assets/generated/loadscreens/mp\\q4dm1_1820x1024.tga", false );
	Expect( "guis/assets/generated/loadscreens/q4dm1_1820x1024.dds", false );
	Expect( "guis/assets/generated/loadscreens/q4dm1_1820x1024.tga.tmp", false );
	Expect( "guis/assets/generated/loadscreens/.tga", false );
	Expect( "guis/assets/generated/loadscreens/", false );
	Expect( "guis/assets/generated/q4dm1_1820x1024.tga", false );
	Expect( "guis/assets/generated/loadscreensx/q4dm1_1820x1024.tga", false );
	Expect( "xguis/assets/generated/loadscreens/q4dm1_1820x1024.tga", false );
	Expect( "_oq4/0123456789abcdef0123456789abcdef.tmp", false );
	Expect( "gfx/guis/loadscreens/q4dm1.tga", false );
	Expect( "guis/assets/splash/addon/q4dm1.tga", false );
	Expect( "generated/images/guis/assets/generated/loadscreens/q4dm1_1820x1024#__0200.bimage", false );
	Expect( "", false );
	Expect( nullptr, false );
	if ( R_IsGeneratedLoadscreenImage( "gfx/guis/loadscreens/q4dm1" ) || R_IsGeneratedLoadscreenImage( nullptr ) ) {
		std::printf( "FAIL: the renderer would purge a stock levelshot\n" );
		++failures;
	}
	if ( failures ) {
		return 1;
	}
	std::puts( "classifier: PASS" );
	return 0;
}
'''

# Names and window shapes the session can produce: a map's base name, or its
# "generated" fallback, and the composite's pixel size.
SESSION_SAMPLES = (
    ("q4dm1", 1820, 1024),
    ("airdefense1", 1920, 1080),
    ("q4ctf_1", 2560, 1080),
    ("generated", 1366, 1024),
    ("a", 1, 1),
)

_COMPILED: dict[str, str] = {}


def compile_and_run(program: str) -> str:
    """Compile and run the classifier program; return an empty string on success."""
    if program in _COMPILED:
        return _COMPILED[program]
    compiler = next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("a C++ compiler is required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pure-loadscreen-", dir=ROOT / ".tmp") as directory:
        source, binary = Path(directory) / "classifier.cpp", Path(directory) / "classifier.exe"
        source.write_text(program, encoding="utf-8")
        built = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)],
                               capture_output=True, text=True)
        if built.returncode != 0:
            result = f"the classifiers no longer compile against the doubles:\n{built.stderr}"
        else:
            ran = subprocess.run([str(binary)], capture_output=True, text=True)
            result = "" if ran.returncode == 0 else f"classifier truth table failed:\n{ran.stdout}{ran.stderr}"
    _COMPILED[program] = result
    return result


def check_classifier(sources: dict[str, str]) -> None:
    classifier = braced(sources["fs"], "static bool FS_IsGeneratedLoadscreenPath(")
    name_test = braced(sources["manager"], "static bool R_IsGeneratedLoadscreenImage(")
    builder = braced(sources["session"], "static idStr Session_MakeExpandedLoadingBackgroundPath(")
    form = re.search(r'return va\( "([^"]*)", safeName\.c_str\(\), width, height \);', builder)
    if form is None or not form.group(1).endswith("%s_%dx%d.tga"):
        raise AssertionError("the session no longer names its backgrounds <map>_<w>x<h>.tga")
    folder = form.group(1)[: -len("%s_%dx%d.tga")]
    if prefix_of(classifier, "the filesystem classifier") != folder:
        raise AssertionError("the filesystem classifier and the session disagree on the folder")
    if prefix_of(name_test, "the renderer's name test") != folder:
        raise AssertionError("the renderer would not purge what the session writes")
    paths = "\n".join(f'\tExpect( "{form.group(1) % sample}", true );' for sample in SESSION_SAMPLES)
    program = (PROGRAM.replace("/*FS_CLASSIFIER*/", classifier)
               .replace("/*RENDERER_NAME_TEST*/", name_test)
               .replace("/*SESSION_PATHS*/", paths))
    failure = compile_and_run(program)
    if failure:
        raise AssertionError(failure)


def check_filesystem(sources: dict[str, str]) -> None:
    fs = sources["fs"]
    allowed = braced(fs, "bool idFileSystemLocal::FileAllowedFromDir(")
    for token in ("generated", "loadscreens"):
        forbid(allowed, token, "FileAllowedFromDir must not admit an expanded loading background while pure")

    search = braced(fs, "idFile *idFileSystemLocal::OpenFileReadSearch(")
    # nothing under the save path is opened while a lock or fs_restrict holds
    require_order(search, "if ( fs_restrict.GetBool() || serverPaks.Num() ) {",
                  "if ( !FileAllowedFromDir( relativePath ) ) {", "the pure directory gate")
    require_order(search, "if ( !FileAllowedFromDir( relativePath ) ) {",
                  "netpath = BuildOSPath( dir->path, dir->gamedir, relativePath );",
                  "the pure gate runs before any directory open")
    classify = "if ( FS_IsGeneratedImageCachePath( relativePath ) || FS_IsGeneratedLoadscreenPath( relativePath ) ) {"
    restart = "} else if ( !loadedFileFromDir && !FileAllowedFromDir( relativePath ) ) {"
    require_order(search, classify, restart, "a loading background read is classified before the restart trigger")
    branch = search[search.index(classify) : search.index(restart)]
    require(branch, "loadedImageCacheFromDir = true;", "a loading background read leaves an eviction")
    forbid(branch, "loadedFileFromDir = true;", "a loading background read must not force the restart")

    pure = braced(fs, "fsPureReply_t idFileSystemLocal::SetPureServerChecksums(")
    require(pure, "if ( loadedFileFromDir ) {\n\t\tsuccess = false;", "any other directory read still restarts")
    require(pure, "if ( success && loadedImageCacheFromDir && serverPaks.Num() ) {\n\t\tpureImageCacheEvictionPending = true;",
            "the eviction is left pending only for a lock actually taken")

    refused = braced(fs, "bool idFileSystemLocal::GeneratedCacheReadsRefused(")
    require(refused, "return fs_restrict.GetBool() || serverPaks.Num() > 0;", "a pure lock and fs_restrict refuse derived data")
    gate = braced(fs, "bool FS_GeneratedDataReadsRefused(")
    require(gate, "return fileSystemLocal.GeneratedCacheReadsRefused();", "the session asks the generated-data gate")
    require(fs, "friend bool\t\t\t\tFS_GeneratedDataReadsRefused( void );", "the gate may read the lock")
    require(sources["fs_h"], "bool\t\t\t\t\t\tFS_GeneratedDataReadsRefused( void );", "the gate is declared for engine code")


def check_session(sources: dict[str, str]) -> None:
    session = sources["session"]
    prepare = braced(session, "static bool Session_PrepareExpandedLoadingBackground(")
    refusal = braced(prepare, "if ( FS_GeneratedDataReadsRefused() ) {")
    require(refusal, "return false;", "nothing is composed while pure")
    for later in ("Sys_GetSecureRandomBytes( stagingNonce, sizeof( stagingNonce ) )",
                  "Session_ResolveImageFilePath( backgroundPath, resolvedCenterPath )",
                  "R_WriteTGA( stagingPath.c_str()",
                  "fileSystem->PromoteFile( stagingPath.c_str(), generatedPath.c_str()"):
        require_order(prepare, "if ( FS_GeneratedDataReadsRefused() ) {", later,
                      "the pure refusal comes before any read or write")
    require_order(prepare, "Session_GetLoadingCanvasExpansion(", "if ( FS_GeneratedDataReadsRefused() ) {",
                  "only an expanding window asks")
    # whatever is published is still shown only if the active VFS exposes it
    require_order(prepare, "} else if ( !Session_FileExistsInSearchPaths( generatedPath.c_str() ) ) {",
                  "declManager->FindMaterial( generatedPath.c_str() )", "the session's VFS visibility gate")

    builder = braced(session, "static idStr Session_MakeExpandedLoadingBackgroundPath(")
    require(builder, "safeName.StripPath();", "a background name is a single segment")
    require(builder, 'safeName = "generated";', "a background name is never empty")

    loading = braced(session, "void idSessionLocal::LoadLoadingGui(")
    require(loading, "if ( Session_PrepareExpandedLoadingBackground( loadingBackground, stripped.c_str(), "
            "expandedLoadingBackground ) ) {\n\t\tloadingBackground = expandedLoadingBackground;",
            "the stock levelshot stays selected unless a background was published")


def check_renderer(sources: dict[str, str]) -> None:
    evict = braced(sources["manager"], "bool idImageManager::ReloadGeneratedCacheImages(")
    purge = "if ( image != NULL && image->IsLoaded() && R_IsGeneratedLoadscreenImage( image->GetName() ) ) {"
    require_order(evict, purge, "if ( image == NULL || !image->HoldsGeneratedCacheContent() ) {",
                  "loading backgrounds are evicted, even when the image cache supplied them")
    block = braced(evict, purge)
    require_order(block, "image->PurgeImage();", "if ( image->IsLoaded() ) {", "each background is checked after its purge")
    forbid(block, "Reload(", "a background has no pure source to reload from")
    require(block, "failed++;\n\t\t\t} else {\n\t\t\t\tpurged++;", "a background left resident fails the eviction")
    require(block, "continue;", "a purged background is not reloaded afterwards")
    require(evict, "return failed == 0;", "a failed eviction makes the client restart")
    require(sources["render_system"], "return globalImages->ReloadGeneratedCacheImages();", "the render system delegates")


def check_client(sources: dict[str, str]) -> None:
    validate = braced(sources["client"], "bool idAsyncClient::ValidatePureServerChecksums(")
    require_order(validate, "fsPureReply_t reply = fileSystem->SetPureServerChecksums(",
                  "if ( reply == PURE_OK && fileSystem->TakePureImageCacheEviction() ) {",
                  "the client asks for the eviction right after taking the lock")
    require_order(validate, "if ( reply == PURE_OK && fileSystem->TakePureImageCacheEviction() ) {",
                  "switch ( reply ) {", "the eviction runs before the reply is acted on")
    require(validate, "if ( renderSystem == NULL || !renderSystem->ReloadGeneratedCacheImages() ) {",
            "the client evicts the derived images")
    require(validate, "reply = PURE_RESTART;", "a failed eviction falls back to the engine restart")


CHECKS = (check_classifier, check_filesystem, check_session, check_renderer, check_client)


def run_checks(sources: dict[str, str]) -> None:
    for check in CHECKS:
        check(sources)


def first_failure(sources: dict[str, str]) -> str:
    """Name the first check that rejects these sources, or return an empty string."""
    for check in CHECKS:
        try:
            check(sources)
        except AssertionError:
            return check.__name__
    return ""


# Each mutation breaks one link: (source key, original text, broken text, owning check).
MUTATIONS: dict[str, tuple[str, str, str, str]] = {
    "subfolders become exempt": (
        "fs", "\t\tif ( *c == '/' || *c == '\\\\' ) {\n\t\t\treturn false;", "\t\tif ( *c == '\\0' ) {\n\t\t\treturn false;",
        "check_classifier"),
    "the size no longer needs its underscore": (
        "fs", "if ( cursor == digits || cursor == name || cursor[ -1 ] != '_' ) {",
        "if ( cursor == digits || cursor == name ) {", "check_classifier"),
    "an empty map name becomes exempt": (
        "fs", "return cursor - 1 > name;", "return cursor - 1 >= name;", "check_classifier"),
    "the filesystem folder widens": (
        "fs", 'static const char prefix[] = "guis/assets/generated/loadscreens/";',
        'static const char prefix[] = "guis/assets/generated/";', "check_classifier"),
    "the renderer folder narrows": (
        "manager", 'static const char prefix[] = "guis/assets/generated/loadscreens/";',
        'static const char prefix[] = "guis/assets/generated/loadscreens/mp/";', "check_classifier"),
    "a background read forces the restart again": (
        "fs", "if ( FS_IsGeneratedImageCachePath( relativePath ) || FS_IsGeneratedLoadscreenPath( relativePath ) ) {",
        "if ( FS_IsGeneratedImageCachePath( relativePath ) ) {", "check_filesystem"),
    "a classified read still counts as a directory read": (
        "fs", "\t\t\t\t\tloadedImageCacheFromDir = true;", "\t\t\t\t\tloadedFileFromDir = true;", "check_filesystem"),
    "the gate ignores the pure lock": (
        "fs", "return fileSystemLocal.GeneratedCacheReadsRefused();", "return false;", "check_filesystem"),
    "the session composes while pure": (
        "session", "\tif ( FS_GeneratedDataReadsRefused() ) {", "\tif ( false ) {", "check_session"),
    "the session drops its VFS gate": (
        "session", "} else if ( !Session_FileExistsInSearchPaths( generatedPath.c_str() ) ) {", "} else if ( false ) {",
        "check_session"),
    "the renderer skips the backgrounds": (
        "manager", "if ( image != NULL && image->IsLoaded() && R_IsGeneratedLoadscreenImage( image->GetName() ) ) {",
        "if ( false ) {", "check_renderer"),
    "the renderer reloads a background instead of purging it": (
        "manager", "\t\t\timage->PurgeImage();\n\t\t\tif ( image->IsLoaded() ) {",
        "\t\t\timage->Reload( true );\n\t\t\tif ( image->IsLoaded() ) {", "check_renderer"),
    "a background left resident passes": (
        "manager", "\t\t\t\tfailed++;\n\t\t\t} else {\n\t\t\t\tpurged++;", "\t\t\t\tpurged++;\n\t\t\t} else {\n\t\t\t\tpurged++;",
        "check_renderer"),
    "the client skips the eviction": (
        "client", "if ( reply == PURE_OK && fileSystem->TakePureImageCacheEviction() ) {", "if ( false ) {", "check_client"),
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--mutations", action="store_true", help="also require each broken link to fail its check")
    args = parser.parse_args()
    sources = {key: read(relative) for key, relative in SOURCES.items()}
    run_checks(sources)
    if args.mutations:
        for name, (key, old, new, owner) in MUTATIONS.items():
            if sources[key].count(old) != 1:
                raise AssertionError(f"mutation {name!r} no longer matches {SOURCES[key]} exactly once")
            mutated = dict(sources)
            mutated[key] = sources[key].replace(old, new)
            caught = first_failure(mutated)
            if caught != owner:
                raise AssertionError(f"mutation {name!r} should fail {owner}, but "
                                     + (f"{caught} failed first" if caught else "nothing failed"))
        print(f"pure_expanded_loadscreen_contract: {len(MUTATIONS)} mutations detected")
    print("pure_expanded_loadscreen_contract: PASS (loading backgrounds refused while pure, evicted instead of a restart)")


if __name__ == "__main__":
    main()
