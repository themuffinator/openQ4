#!/usr/bin/env python3
"""Pin how the session's expanded loading background meets pure servers.

In a window that is not 4:3 the session composes the map's levelshot, with any
_left/_right/_top/_bottom art, into one picture of the window's shape. Until
9 November 2026 it published that picture under fs_savepath as
guis/assets/generated/loadscreens/<map>_<w>x<h>.tga and read it back through
the search paths, so a pure lock had to refuse it and pure clients saw the
framed 4:3 levelshot. The rules decided then, recorded in
docs/dev/mp-network-qualification.md:

* The picture lives only in memory. The session uploads it to a scratch image
  whose name starts with '_' (the stock GUI's implicit material and the
  retained screen's _retained/ material find such an image by name whatever
  their image options) and carries the picture's size, because the retained
  interface keeps an image's dimensions by name. It writes nothing and asks no
  pure gate: its sources are VFS reads the lock already admits, so pure
  clients see the wide background too.
* The session keeps the pixels and uploads them again when the video restart
  count moves, before any GUI draws, since a restart reallocates scratch images
  empty. A failed upload leaves the stock levelshot selected.
* Files older builds left in that folder are never named again, but a stray
  reference can still open one. Such a read before the lock still sets
  loadedImageCacheFromDir rather than loadedFileFromDir, and the client's
  ReloadGeneratedCacheImages purges every resident image from the folder; a
  purge that fails restarts the engine. Every other directory read restarts.

This test compiles the real filesystem classifier and the renderer's name test
against the names those builds wrote and against near misses, then pins each
link in the production source. --mutations breaks each link and requires the
check that owns it to fail.
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
    "material": "src/renderer/Material.cpp",
    "document": "src/ui/retained/Document.cpp",
}

# What builds before 9 November 2026 wrote (Session_MakeExpandedLoadingBackgroundPath).
LEGACY_FORMAT = "guis/assets/generated/loadscreens/%s_%dx%d.tga"
LEGACY_FOLDER = LEGACY_FORMAT[: -len("%s_%dx%d.tga")]

TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/|("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')', re.S)
PREFIX = re.compile(r'static const char prefix\[\] = "([^"]*)";')
IMAGE_NAME = re.compile(r'#define SESSION_LOADING_BACKGROUND_IMAGE\s+"([^"]*)"')


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8").replace("\r\n", "\n")


def code_only(source: str) -> str:
    """Drop comments, keeping string and character literals."""
    return COMMENT.sub(lambda match: match.group(1) or "", source)


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
		std::printf( "FAIL: %s should %sbe exempt as a legacy expanded loading background\n", path ? path : "(null)", expected ? "" : "not " );
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
	// what builds before 9 November 2026 wrote
/*LEGACY_PATHS*/
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
	Expect( "_expandedLoadingBackground_1820x1024", false );
	Expect( "gfx/guis/loadscreens/q4dm1.tga", false );
	Expect( "guis/assets/splash/addon/q4dm1.tga", false );
	Expect( "generated/images/guis/assets/generated/loadscreens/q4dm1_1820x1024#__0200.bimage", false );
	Expect( "", false );
	Expect( nullptr, false );
	if ( R_IsGeneratedLoadscreenImage( "gfx/guis/loadscreens/q4dm1" ) || R_IsGeneratedLoadscreenImage( nullptr ) ||
		R_IsGeneratedLoadscreenImage( "_expandedLoadingBackground_1820x1024" ) ) {
		std::printf( "FAIL: the renderer would purge a stock levelshot or the session's in-memory background\n" );
		++failures;
	}
	if ( failures ) {
		return 1;
	}
	std::puts( "classifier: PASS" );
	return 0;
}
'''

# Names and window shapes the older builds produced: a map's base name, or its
# "generated" fallback, and the composite's pixel size.
LEGACY_SAMPLES = (
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
    if prefix_of(classifier, "the filesystem classifier") != LEGACY_FOLDER:
        raise AssertionError("the filesystem classifier no longer covers the folder older builds wrote")
    if prefix_of(name_test, "the renderer's name test") != LEGACY_FOLDER:
        raise AssertionError("the renderer would not purge what older builds wrote")
    paths = "\n".join(f'\tExpect( "{LEGACY_FORMAT % sample}", true );' for sample in LEGACY_SAMPLES)
    program = (PROGRAM.replace("/*FS_CLASSIFIER*/", classifier)
               .replace("/*RENDERER_NAME_TEST*/", name_test)
               .replace("/*LEGACY_PATHS*/", paths))
    failure = compile_and_run(program)
    if failure:
        raise AssertionError(failure)


def check_filesystem(sources: dict[str, str]) -> None:
    fs = sources["fs"]
    allowed = braced(fs, "bool idFileSystemLocal::FileAllowedFromDir(")
    for token in ("generated", "loadscreens"):
        forbid(allowed, token, "FileAllowedFromDir must not admit a legacy loading background while pure")

    search = braced(fs, "idFile *idFileSystemLocal::OpenFileReadSearch(")
    # nothing under the save path is opened while a lock or fs_restrict holds
    require_order(search, "if ( fs_restrict.GetBool() || serverPaks.Num() ) {",
                  "if ( !FileAllowedFromDir( relativePath ) ) {", "the pure directory gate")
    require_order(search, "if ( !FileAllowedFromDir( relativePath ) ) {",
                  "netpath = BuildOSPath( dir->path, dir->gamedir, relativePath );",
                  "the pure gate runs before any directory open")
    classify = "if ( FS_IsGeneratedImageCachePath( relativePath ) || FS_IsGeneratedLoadscreenPath( relativePath ) ) {"
    restart = "} else if ( !loadedFileFromDir && !FileAllowedFromDir( relativePath ) ) {"
    require_order(search, classify, restart, "a legacy loading background read is classified before the restart trigger")
    branch = search[search.index(classify) : search.index(restart)]
    require(branch, "loadedImageCacheFromDir = true;", "a legacy loading background read leaves an eviction")
    forbid(branch, "loadedFileFromDir = true;", "a legacy loading background read must not force the restart")

    pure = braced(fs, "fsPureReply_t idFileSystemLocal::SetPureServerChecksums(")
    require(pure, "if ( loadedFileFromDir ) {\n\t\tsuccess = false;", "any other directory read still restarts")
    require(pure, "if ( success && loadedImageCacheFromDir && serverPaks.Num() ) {\n\t\tpureImageCacheEvictionPending = true;",
            "the eviction is left pending only for a lock actually taken")


def check_session(sources: dict[str, str]) -> None:
    session = sources["session"]
    code = code_only(session)
    forbid(code, LEGACY_FOLDER, "the session must not name the folder older builds published to")
    forbid(code, "FS_GeneratedDataReadsRefused", "the session must not refuse the background while pure")

    name = IMAGE_NAME.search(session)
    if name is None:
        raise AssertionError("the session no longer names its loading background image")
    if not re.fullmatch(r"_[A-Za-z0-9_]+", name.group(1)):
        raise AssertionError("the loading background image name must start with '_' and stay a plain retained "
                             "image source, so every material finds the built-in by name")

    prepare = code_only(braced(session, "static bool Session_PrepareExpandedLoadingBackground("))
    for writer in ("R_WriteTGA(", "PromoteFile(", "WriteFile(", "OpenFileWrite(", "Sys_GetSecureRandomBytes",
                   '"fs_savepath"', "GeneratedCacheReadsRefused", "serverPaks", "TakePureImageCacheEviction"):
        forbid(prepare, writer, "the background is composed in memory, not published or gated by the pure lock")
    # the sources resolve and load through the active VFS
    require_order(prepare, "Session_ResolveImageFilePath( backgroundPath, resolvedCenterPath )",
                  "Session_LoadResolvedImageRGBA( resolvedCenterPath, centerPic, centerWidth, centerHeight )",
                  "the levelshot is read through the search paths")
    require(prepare, 'va( SESSION_LOADING_BACKGROUND_IMAGE "_%dx%d", outputWidth, outputHeight )',
            "the image name carries the picture's size")
    require_order(prepare, 'va( SESSION_LOADING_BACKGROUND_IMAGE "_%dx%d", outputWidth, outputHeight )',
                  "bool uploaded = Session_UploadLoadingBackground();", "the picture is named before it is uploaded")
    require_order(prepare, "bool uploaded = Session_UploadLoadingBackground();",
                  "declManager->FindMaterial( name.c_str() )", "the uploaded image's material is checked")
    require(prepare, "material->TestMaterialFlag( MF_DEFAULTED )", "a defaulted material is refused")
    require(prepare, "\tif ( !uploaded ) {\n\t\tSession_ReleaseLoadingBackground( true );\n\t\treturn false;",
            "a failed upload leaves the stock levelshot selected")
    require_order(prepare, "\tif ( !uploaded ) {\n\t\tSession_ReleaseLoadingBackground( true );\n\t\treturn false;",
                  "\n\timageName = name;\n\treturn true;", "the name is handed out only after the upload succeeded")

    upload = code_only(braced(session, "static bool Session_UploadLoadingBackground("))
    for snippet in ("options.format = FMT_RGBA8;", "options.numLevels = 1;", "options.isPersistant = true;",
                    "renderSystem->CreateImage( background.imageName.c_str(), &options, TF_LINEAR )",
                    "renderSystem->GetImageSize( image, width, height );",
                    "renderSystem->UploadImage( background.imageName.c_str(), background.pixels.Ptr(), background.width, background.height )"):
        require(upload, snippet, "the picture is a one-level RGBA8 scratch image of exactly its size")
    require_order(upload, "renderSystem->UploadImage(", "background.restartCount = renderSystem->GetVideoRestartCount();",
                  "an upload records the video restart it survives")

    restore = code_only(braced(session, "static void Session_RestoreLoadingBackground("))
    require(restore, "renderSystem->GetVideoRestartCount() == background.restartCount",
            "a video restart is noticed")
    require(restore, "if ( !Session_UploadLoadingBackground() ) {", "the kept pixels are uploaded again")
    draw = braced(session, "void idSessionLocal::Draw() {")
    require_order(draw, "Session_RestoreLoadingBackground();", "guiLoading->Redraw( presentationTime );",
                  "the pixels are back before the loading screen draws")
    require_order(draw, "Session_RestoreLoadingBackground();", "guiLoadingHold->Redraw( presentationTime );",
                  "the pixels are back before a held loading screen draws")

    loading = braced(session, "void idSessionLocal::LoadLoadingGui(")
    require(loading, "if ( Session_PrepareExpandedLoadingBackground( loadingBackground, expandedLoadingBackground ) ) {\n"
            "\t\tloadingBackground = expandedLoadingBackground;", "the stock levelshot stays selected unless one was uploaded")
    require(loading, "} else {\n\t\tSession_ReleaseLoadingBackground( true );", "a load without one keeps no picture")


def check_lookup(sources: dict[str, str]) -> None:
    manager = sources["manager"]
    for signature in ("idImage\t*idImageManager::ImageFromFile(", "idImage *idImageManager::ImageHandleDeferred("):
        body = braced(manager, signature)
        require(body, "if ( name[0] == '_' ) {\n\t\t\t\treturn image;",
                f"{signature} must return a '_' built-in whatever the material's image options")
    retained = braced(sources["material"], "if ( retainedAdditive || idStr::Icmpn( GetName(), \"_retained/\", 10 ) == 0 ) {")
    require(retained, "*p == '_'", "a retained material accepts the background's image name")
    valid = braced(sources["document"], "bool ValidImageSource(const std::string& source) {")
    require(valid, "c == '_'", "the retained screen accepts the background's image name as a source")


def check_renderer(sources: dict[str, str]) -> None:
    evict = braced(sources["manager"], "bool idImageManager::ReloadGeneratedCacheImages(")
    purge = "if ( image != NULL && image->IsLoaded() && R_IsGeneratedLoadscreenImage( image->GetName() ) ) {"
    require_order(evict, purge, "if ( image == NULL || !image->HoldsGeneratedCacheContent() ) {",
                  "legacy loading backgrounds are evicted, even when the image cache supplied them")
    block = braced(evict, purge)
    require_order(block, "image->PurgeImage();", "if ( image->IsLoaded() ) {", "each background is checked after its purge")
    forbid(block, "Reload(", "a legacy background has no pure source to reload from")
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


CHECKS = (check_classifier, check_filesystem, check_session, check_lookup, check_renderer, check_client)


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
    "a legacy background read forces the restart again": (
        "fs", "if ( FS_IsGeneratedImageCachePath( relativePath ) || FS_IsGeneratedLoadscreenPath( relativePath ) ) {",
        "if ( FS_IsGeneratedImageCachePath( relativePath ) ) {", "check_filesystem"),
    "a classified read still counts as a directory read": (
        "fs", "\t\t\t\t\tloadedImageCacheFromDir = true;", "\t\t\t\t\tloadedFileFromDir = true;", "check_filesystem"),
    "the session publishes to the save path again": (
        "session", "\tbool uploaded = Session_UploadLoadingBackground();",
        '\tR_WriteTGA( "_oq4/stage.tmp", background.pixels.Ptr(), outputWidth, outputHeight, false, "fs_savepath" );\n'
        "\tbool uploaded = Session_UploadLoadingBackground();", "check_session"),
    "the session refuses the background while pure again": (
        "session", "renderSystem == NULL || !renderSystem->IsOpenGLRunning() ) {",
        "renderSystem == NULL || !renderSystem->IsOpenGLRunning() || FS_GeneratedDataReadsRefused() ) {", "check_session"),
    "the image name loses its underscore": (
        "session", '#define SESSION_LOADING_BACKGROUND_IMAGE\t"_expandedLoadingBackground"',
        '#define SESSION_LOADING_BACKGROUND_IMAGE\t"expandedLoadingBackground"', "check_session"),
    "the image name loses its size": (
        "session", 'va( SESSION_LOADING_BACKGROUND_IMAGE "_%dx%d", outputWidth, outputHeight )',
        "idStr( SESSION_LOADING_BACKGROUND_IMAGE )", "check_session"),
    "the upload forgets the video restart": (
        "session", "\tbackground.restartCount = renderSystem->GetVideoRestartCount();\n\treturn true;",
        "\treturn true;", "check_session"),
    "a video restart is never noticed": (
        "session", "renderSystem->GetVideoRestartCount() == background.restartCount",
        "background.restartCount == background.restartCount", "check_session"),
    "the session draws before restoring the pixels": (
        "session", "\tSession_RestoreLoadingBackground();\n\tstatic const int fallbackMenuDelayMs = 3000;",
        "\tstatic const int fallbackMenuDelayMs = 3000;", "check_session"),
    "a failed upload still selects the background": (
        "session", "\tif ( !uploaded ) {\n\t\tSession_ReleaseLoadingBackground( true );\n\t\treturn false;",
        "\tif ( !uploaded ) {\n\t\tSession_ReleaseLoadingBackground( true );", "check_session"),
    "the material lookup honours options for built-ins": (
        "manager", "\t\t\tif ( name[0] == '_' ) {\n\t\t\t\treturn image;\n\t\t\t}\n\t\t\tif ( image->cubeFiles != cubeMap ) {\n"
        "\t\t\t\tcommon->Error( \"Image '%s' has been referenced with conflicting cube map states\", _name );\n\t\t\t}\n\n",
        "\t\t\tif ( image->cubeFiles != cubeMap ) {\n"
        "\t\t\t\tcommon->Error( \"Image '%s' has been referenced with conflicting cube map states\", _name );\n\t\t\t}\n\n",
        "check_lookup"),
    "the renderer skips the legacy backgrounds": (
        "manager", "if ( image != NULL && image->IsLoaded() && R_IsGeneratedLoadscreenImage( image->GetName() ) ) {",
        "if ( false ) {", "check_renderer"),
    "the renderer reloads a legacy background instead of purging it": (
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
    print("pure_expanded_loadscreen_contract: PASS (wide loading background composed in memory, "
          "legacy files evicted instead of a restart)")


if __name__ == "__main__":
    main()
