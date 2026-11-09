#!/usr/bin/env python3
"""Pin how a pure lock treats openQ4's generated level-load caches.

With com_levelLoadModernization 1 (experimental, off by default) openQ4 reads
derived render-model, render-world and collision caches, and the game modules
read a derived animation cache, from <fs_savepath>/<gameDir>/generated/. Their
owners open these files with OpenExplicitFileRead, outside the search paths, so
the pure directory gate in OpenFileReadSearch never sees them. An entry names
its source by public pak metadata and binds its payload to nothing, so a forged
entry passes every load check. The rules that follow, decided on 9 November
2026 for the image cache and recorded in docs/dev/mp-network-qualification.md:

* While a pure lock (serverPaks) or fs_restrict holds, the filesystem refuses
  every generated-cache read: GeneratedCacheReadsEnabled answers false for
  every kind, OpenGeneratedCacheRead returns nothing, and the game's animation
  reader asks the same gate before it opens a file.
* Entries are not written under a pure lock. Their content key carries the
  lock, and nothing is read while it holds, so they could never be read back.
* A resident decoded before the lock is derived again from its source at the
  next level load: the model manager purges cache-sourced models (whatever
  reloadable flag the payload carried), the render world is not retained, the
  collision manager purges cache-sourced models, and the game reloads
  cache-sourced animations in place.
* The learned manifests stay readable. They only schedule reads, and each entry
  is opened again through the pure search paths and must match its recorded
  identity before its bytes are used.

The gate functions are compiled against small doubles and run through a truth
table; every eviction link is pinned in the production source. --mutations
breaks each link in the extracted source and requires the checks to fail.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from typing import Callable

ROOT = Path(__file__).resolve().parents[2]

SOURCES = {
    "fs": "src/framework/FileSystem.cpp",
    "fs_h": "src/framework/FileSystem.h",
    "manager": "src/framework/LevelLoadCacheManager.cpp",
    "model": "src/renderer/Model.cpp",
    "model_h": "src/renderer/Model_local.h",
    "md5": "src/renderer/Model_md5.cpp",
    "md5r": "src/renderer/Model_md5r.cpp",
    "model_manager": "src/renderer/ModelManager.cpp",
    "world": "src/renderer/RenderWorld_load.cpp",
    "world_ctor": "src/renderer/RenderWorld.cpp",
    "cm_load": "src/cm/CollisionModel_load.cpp",
    "cm_files": "src/cm/CollisionModel_files.cpp",
    "anim_sp": "src/game/anim/Anim.cpp",
    "anim_mp": "src/mpgame/anim/Anim.cpp",
    "game_sp": "src/game/Game_local.cpp",
    "game_mp": "src/mpgame/Game_local.cpp",
}

TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)


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


PROGRAM = r'''
#include <cstdio>
#include <memory>

#ifndef NULL
#define NULL nullptr
#endif

/*KIND_ENUM*/

struct Flag {
	bool value = false;
	bool GetBool() const { return value; }
};
struct PakList {
	int count = 0;
	int Num() const { return count; }
};

namespace {
static Flag com_levelLoadModernization;
static Flag com_levelLoadCache;
static Flag com_levelLoadCacheWrite;
/*IS_VALID_CACHE_KIND*/
}

class idLevelLoadCacheManager {
public:
	struct Impl {};
	std::unique_ptr<Impl> impl{ new Impl };
	bool GeneratedCacheReadsEnabled( generatedCacheKind_t kind ) const;
	bool GeneratedCacheWritesEnabled( generatedCacheKind_t kind ) const;
};

/*MANAGER_READS*/

/*MANAGER_WRITES*/

class idFileSystemLocal {
public:
	Flag fs_restrict;
	PakList serverPaks;
	idLevelLoadCacheManager *levelLoadCache = nullptr;
	bool GeneratedCacheReadsRefused( void ) const;
	bool GeneratedCacheReadsEnabled( generatedCacheKind_t kind ) const;
	bool GeneratedCacheWritesEnabled( generatedCacheKind_t kind ) const;
};

/*FS_REFUSED*/

/*FS_READS*/

/*FS_WRITES*/

static int failures = 0;
static void Expect( bool condition, const char *what, int kind ) {
	if ( !condition ) {
		std::printf( "FAIL: %s (kind %d)\n", what, kind );
		++failures;
	}
}

int main() {
	const generatedCacheKind_t kinds[] = {
		GENERATED_CACHE_RENDER_MODEL, GENERATED_CACHE_RENDER_WORLD,
		GENERATED_CACHE_COLLISION_MODEL, GENERATED_CACHE_ANIMATION };
	idLevelLoadCacheManager manager;
	idFileSystemLocal fs;

	for ( generatedCacheKind_t kind : kinds ) {
		Expect( !fs.GeneratedCacheReadsEnabled( kind ), "no coordinator, no reads", kind );
		Expect( !fs.GeneratedCacheWritesEnabled( kind ), "no coordinator, no writes", kind );
	}
	fs.levelLoadCache = &manager;
	com_levelLoadCache.value = true;
	com_levelLoadCacheWrite.value = true;
	for ( generatedCacheKind_t kind : kinds ) {
		Expect( !fs.GeneratedCacheReadsEnabled( kind ), "com_levelLoadModernization 0 reads nothing", kind );
		Expect( !fs.GeneratedCacheWritesEnabled( kind ), "com_levelLoadModernization 0 writes nothing", kind );
	}

	com_levelLoadModernization.value = true;
	for ( generatedCacheKind_t kind : kinds ) {
		Expect( fs.GeneratedCacheReadsEnabled( kind ), "an open session reads every kind", kind );
		Expect( fs.GeneratedCacheWritesEnabled( kind ) == ( kind != GENERATED_CACHE_ANIMATION ),
			"the coordinator writes only its envelope kinds", kind );
	}
	Expect( !fs.GeneratedCacheReadsEnabled( static_cast<generatedCacheKind_t>( 99 ) ), "unknown kinds are not read", 99 );
	Expect( !fs.GeneratedCacheWritesEnabled( static_cast<generatedCacheKind_t>( 99 ) ), "unknown kinds are not written", 99 );

	com_levelLoadCache.value = false;
	for ( generatedCacheKind_t kind : kinds ) {
		Expect( fs.GeneratedCacheReadsEnabled( kind ) == ( kind == GENERATED_CACHE_ANIMATION ),
			"com_levelLoadCache 0 leaves only the game's animation cache", kind );
		Expect( !fs.GeneratedCacheWritesEnabled( kind ), "com_levelLoadCache 0 writes nothing", kind );
	}
	com_levelLoadCache.value = true;
	com_levelLoadCacheWrite.value = false;
	for ( generatedCacheKind_t kind : kinds ) {
		Expect( fs.GeneratedCacheReadsEnabled( kind ), "com_levelLoadCacheWrite 0 still reads", kind );
		Expect( !fs.GeneratedCacheWritesEnabled( kind ), "com_levelLoadCacheWrite 0 writes nothing", kind );
	}
	com_levelLoadCacheWrite.value = true;

	fs.serverPaks.count = 3;
	Expect( fs.GeneratedCacheReadsRefused(), "a pure lock refuses generated-cache reads", 0 );
	for ( generatedCacheKind_t kind : kinds ) {
		Expect( !fs.GeneratedCacheReadsEnabled( kind ), "nothing is read under a pure lock", kind );
		Expect( !fs.GeneratedCacheWritesEnabled( kind ), "nothing is written under a pure lock", kind );
	}
	fs.serverPaks.count = 0;

	fs.fs_restrict.value = true;
	Expect( fs.GeneratedCacheReadsRefused(), "fs_restrict refuses generated-cache reads", 0 );
	for ( generatedCacheKind_t kind : kinds ) {
		Expect( !fs.GeneratedCacheReadsEnabled( kind ), "nothing is read under fs_restrict", kind );
		Expect( fs.GeneratedCacheWritesEnabled( kind ) == ( kind != GENERATED_CACHE_ANIMATION ),
			"fs_restrict still writes", kind );
	}
	fs.fs_restrict.value = false;

	Expect( !fs.GeneratedCacheReadsRefused(), "clearing the lock lifts the refusal", 0 );
	for ( generatedCacheKind_t kind : kinds ) {
		Expect( fs.GeneratedCacheReadsEnabled( kind ), "reads return once the lock is cleared", kind );
	}

	if ( failures ) {
		return 1;
	}
	std::puts( "gate truth table: PASS" );
	return 0;
}
'''


_GATE_RESULTS: dict[str, str] = {}


def check_gates(sources: dict[str, str]) -> None:
    fs, manager = sources["fs"], sources["manager"]
    enum = re.search(r"typedef enum \{[^}]*\} generatedCacheKind_t;", sources["fs_h"])
    if enum is None:
        raise AssertionError("missing the generatedCacheKind_t enum")
    require_order(enum.group(), "GENERATED_CACHE_COLLISION_MODEL,", "GENERATED_CACHE_ANIMATION",
                  "the animation kind is appended, so existing values keep their numbers")
    program = PROGRAM
    for marker, text in (
        ("/*KIND_ENUM*/", enum.group()),
        ("/*IS_VALID_CACHE_KIND*/", braced(manager, "bool IsValidCacheKind(")),
        ("/*MANAGER_READS*/", braced(manager, "bool idLevelLoadCacheManager::GeneratedCacheReadsEnabled(")),
        ("/*MANAGER_WRITES*/", braced(manager, "bool idLevelLoadCacheManager::GeneratedCacheWritesEnabled(")),
        ("/*FS_REFUSED*/", braced(fs, "bool idFileSystemLocal::GeneratedCacheReadsRefused(")),
        ("/*FS_READS*/", braced(fs, "bool idFileSystemLocal::GeneratedCacheReadsEnabled(")),
        ("/*FS_WRITES*/", braced(fs, "bool idFileSystemLocal::GeneratedCacheWritesEnabled(")),
    ):
        program = program.replace(marker, text)
    # --mutations reruns this for every mutation; most leave the gates alone
    if program not in _GATE_RESULTS:
        _GATE_RESULTS[program] = run_truth_table(program)
    if _GATE_RESULTS[program]:
        raise AssertionError(_GATE_RESULTS[program])


def run_truth_table(program: str) -> str:
    """Compile and run the gate program; return an empty string on success."""
    compiler = next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("a C++ compiler is required")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pure-level-load-cache-", dir=ROOT / ".tmp") as directory:
        source, binary = Path(directory) / "gates.cpp", Path(directory) / "gates.exe"
        source.write_text(program, encoding="utf-8")
        built = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)],
                               capture_output=True, text=True)
        if built.returncode != 0:
            return f"the gate functions no longer compile against the doubles:\n{built.stderr}"
        ran = subprocess.run([str(binary)], capture_output=True, text=True)
        if ran.returncode != 0:
            return f"gate truth table failed:\n{ran.stdout}{ran.stderr}"
    return ""


def check_reads(sources: dict[str, str]) -> None:
    fs, manager = sources["fs"], sources["manager"]
    open_read = braced(fs, "idFile *idFileSystemLocal::OpenGeneratedCacheRead(")
    require_order(open_read, "if ( !GeneratedCacheReadsEnabled( kind ) ) {", "levelLoadCache->OpenGeneratedCacheRead(",
                  "the filesystem gate runs before the coordinator reads")
    if fs.count("levelLoadCache->OpenGeneratedCacheRead(") != 1:
        raise AssertionError("only the gated filesystem wrapper may reach the coordinator's cache read")
    coordinator = braced(manager, "idFile *idLevelLoadCacheManager::OpenGeneratedCacheRead(")
    require_order(coordinator, "!IsValidCacheKind( kind ) || !GeneratedCacheReadsEnabled( kind )",
                  "ReadWholeFile( impl->fileSystem, cachePath.c_str()",
                  "the coordinator reads only its envelope kinds")
    # the manifest only schedules: every entry goes back through the pure VFS
    replay = braced(manager, "void idLevelLoadCacheManager::Begin(")
    require_order(replay, "impl->fileSystem->OpenFileRead(\n\t\t\t\t\t\tentry.source.normalizedPath.c_str(), false );",
                  "current != entry.source", "manifest entries are reopened through the search paths and matched")
    if manager.count("ReadWholeFile( impl->fileSystem,") != 2:
        raise AssertionError("a new generated-file read in the coordinator needs its own pure decision")
    for relative, text in sources.items():
        if relative in ("fs", "manager"):
            continue
        for match in re.finditer(r"(\S+)OpenGeneratedCacheRead\(", text):
            if not match.group(1).endswith("fileSystem->"):
                raise AssertionError(f"{SOURCES[relative]} reaches OpenGeneratedCacheRead without the filesystem gate")


def check_render_models(sources: dict[str, str]) -> None:
    model, md5, md5r = sources["model"], sources["md5"], sources["md5r"]
    try_read = braced(model, "bool R_TryReadGeneratedRenderModelCache(")
    require_order(try_read, "cacheModel->ReadLevelLoadCachePayload( *cache );",
                  "cacheModel->SetGeneratedCacheContent( true );", "a decoded payload marks the model")
    require(braced(model, "void idRenderModelStatic::PurgeModel("), "generatedCacheContent = false;",
            "purging a static model clears its provenance")
    require(braced(model, "idRenderModelStatic::idRenderModelStatic() {"), "generatedCacheContent = false;",
            "a new model holds no cache content")
    require(braced(md5, "void idRenderModelMD5::PurgeModel("), "generatedCacheContent = false;",
            "purging an MD5 model clears its provenance")
    require(braced(md5r, "void rvRenderModelMD5R::PurgeModel("), "idRenderModelStatic::PurgeModel();",
            "MD5R purges through the static model")
    for signature in ("bool rvRenderModelMD5R::InitFromMD5Model(", "bool rvRenderModelMD5R::InitFromStaticModelInternal("):
        require(braced(md5r, signature), "SetGeneratedCacheContent( sourceModel.HoldsGeneratedCacheContent() );",
                f"{signature} carries its source's provenance")
    begin = braced(sources["model_manager"], "void idRenderModelManagerLocal::BeginLevelLoad(")
    require_order(begin, "!fileSystem->GeneratedCacheReadsEnabled( GENERATED_CACHE_RENDER_MODEL );",
                  "staticModel->HoldsGeneratedCacheContent()", "the purge asks the filesystem gate")
    require(begin, "if ( ( com_purgeAll.GetBool() && model->IsReloadable() ) || cacheContent ) {",
            "cache-sourced models are purged whatever reloadable flag their payload carried")
    require_order(begin, "model->PurgeModel();", "model->SetLevelLoadReferenced( false );",
                  "the purge happens before the level references models")


def check_world(sources: dict[str, str]) -> None:
    world = sources["world"]
    init = braced(world, "bool idRenderWorldLocal::InitFromMap(")
    require_order(init, "const bool retainedWorldRefused = worldFromGeneratedCache\n\t\t&& !fileSystem->GeneratedCacheReadsEnabled( GENERATED_CACHE_RENDER_WORLD );",
                  "if ( name == mapName && !retainedWorldRefused ) {", "a cache-sourced world is not retained once refused")
    require_order(init, "if ( loadedFromCache ) {\n\t\t\t\tworldFromGeneratedCache = true;",
                  "Loaded generated render-world cache", "a world decoded from the cache is marked")
    require(braced(world, "void idRenderWorldLocal::FreeWorld("), "worldFromGeneratedCache = false;",
            "freeing the world clears its provenance")
    require(braced(sources["world_ctor"], "idRenderWorldLocal::idRenderWorldLocal() {"),
            "worldFromGeneratedCache = false;", "a new world holds no cache content")


def check_collision(sources: dict[str, str]) -> None:
    cm_load, cm_files = sources["cm_load"], sources["cm_files"]
    load_map = braced(cm_load, "void idCollisionModelManagerLocal::LoadMap(")
    require_order(load_map, "if ( !fileSystem->GeneratedCacheReadsEnabled( GENERATED_CACHE_COLLISION_MODEL ) ) {",
                  "PurgeGeneratedCacheModels();", "LoadMap purges cache-sourced collision once refused")
    require_order(load_map, "PurgeGeneratedCacheModels();", "worldModel = static_cast<idCollisionModelLocal *>( FindModel(",
                  "the purge runs before the world model is reused")
    purge = braced(cm_load, "int idCollisionModelManagerLocal::PurgeGeneratedCacheModels(")
    require_order(purge, "!model->fromGeneratedCache", "FreeModelMemory( model );", "only cache-sourced models are freed")
    require(braced(cm_load, "void idCollisionModelManagerLocal::FreeModelMemory("), "model->fromGeneratedCache = false;",
            "freed models lose their provenance")
    require(braced(cm_load, "idCollisionModelLocal *idCollisionModelManagerLocal::AllocModel("),
            "model->fromGeneratedCache = false;", "new models hold no cache content")
    adopt = braced(cm_files, "bool idCollisionModelManagerLocal::LoadGeneratedCollisionCache(")
    require_order(adopt, "idCollisionModelLocal *model = AllocModel();", "model->fromGeneratedCache = true;",
                  "models built from the cache are marked")


def check_animations(sources: dict[str, str]) -> None:
    for tree in ("sp", "mp"):
        anim, game = sources[f"anim_{tree}"], sources[f"game_{tree}"]
        reader = braced(anim, "bool idMD5Anim::LoadGeneratedAnim(")
        require_order(reader, "if ( !fileSystem->GeneratedCacheReadsEnabled( GENERATED_CACHE_ANIMATION ) ) {",
                      "fileSystem->OpenExplicitFileRead( cacheOSPath );", f"{tree}: the animation reader asks the gate first")
        require_order(reader, "Free();\n\tname = filename;\n\tfromGeneratedCache = true;", "return true;",
                      f"{tree}: an animation decoded from the cache is marked")
        require(braced(anim, "void idMD5Anim::Free("), "fromGeneratedCache = false;",
                f"{tree}: freeing (and so every source parse) clears the provenance")
        require(braced(anim, "idMD5Anim::idMD5Anim() {"), "fromGeneratedCache = false;",
                f"{tree}: a new animation holds no cache content")
        reload = braced(anim, "int idAnimManager::ReloadGeneratedCacheAnims(")
        require_order(reload, "!( *animptr )->FromGeneratedCache()", "( *animptr )->Reload()",
                      f"{tree}: only cache-sourced animations are reloaded, in place")
        load_map = braced(game, "void idGameLocal::LoadMap(")
        require_order(load_map, "if ( !fileSystem->GeneratedCacheReadsEnabled( GENERATED_CACHE_ANIMATION ) ) {",
                      "animationLib->ReloadGeneratedCacheAnims();", f"{tree}: LoadMap reloads them once refused")
        require_order(load_map, "animationLib->ReloadGeneratedCacheAnims();", "collisionModelManager->LoadMap( mapFile, false );",
                      f"{tree}: animations are re-derived before the map spawns anything")
    for signature in ("bool idMD5Anim::LoadGeneratedAnim(", "int idAnimManager::ReloadGeneratedCacheAnims(",
                      "void idMD5Anim::Free("):
        if braced(sources["anim_sp"], signature) != braced(sources["anim_mp"], signature):
            raise AssertionError(f"single-player and multiplayer {signature} must stay identical")


CHECKS: tuple[Callable[[dict[str, str]], None], ...] = (
    check_gates, check_reads, check_render_models, check_world, check_collision, check_animations,
)


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


# Each mutation breaks one link; MUTATION_OWNERS names the check that must catch it.
MUTATIONS: dict[str, tuple[str, str, str]] = {
    "pure lock no longer refuses reads": (
        "fs", "return fs_restrict.GetBool() || serverPaks.Num() > 0;", "return fs_restrict.GetBool();"),
    "fs_restrict no longer refuses reads": (
        "fs", "return fs_restrict.GetBool() || serverPaks.Num() > 0;", "return serverPaks.Num() > 0;"),
    "read gate skips the refusal": (
        "fs", "\tif ( GeneratedCacheReadsRefused() ) {\n\t\treturn false;\n\t}\n\treturn levelLoadCache != NULL && levelLoadCache->GeneratedCacheReadsEnabled( kind );",
        "\treturn levelLoadCache != NULL && levelLoadCache->GeneratedCacheReadsEnabled( kind );"),
    "writes continue under a pure lock": (
        "fs", "\tif ( serverPaks.Num() > 0 ) {\n\t\treturn false;\n\t}\n\treturn levelLoadCache != NULL && levelLoadCache->GeneratedCacheWritesEnabled( kind );",
        "\treturn levelLoadCache != NULL && levelLoadCache->GeneratedCacheWritesEnabled( kind );"),
    "coordinator writes the animation kind": (
        "manager", "\tif ( !IsValidCacheKind( kind ) ) {\n\t\treturn false;\n\t}\n\treturn GeneratedCacheReadsEnabled( kind ) && com_levelLoadCacheWrite.GetBool();",
        "\treturn GeneratedCacheReadsEnabled( kind ) && com_levelLoadCacheWrite.GetBool();"),
    "animation reads ignore com_levelLoadModernization": (
        "manager", "\tif ( impl == nullptr || !com_levelLoadModernization.GetBool() ) {\n\t\treturn false;\n\t}\n\t// The game module",
        "\tif ( impl == nullptr ) {\n\t\treturn false;\n\t}\n\t// The game module"),
    "filesystem wrapper skips its gate": (
        "fs", "\tif ( !GeneratedCacheReadsEnabled( kind ) ) {\n\t\treturn NULL;\n\t}\n\tidStr contentKey;\n\tBuildLevelLoadContentKey( contentKey );\n\treturn levelLoadCache->OpenGeneratedCacheRead(",
        "\tidStr contentKey;\n\tBuildLevelLoadContentKey( contentKey );\n\treturn levelLoadCache->OpenGeneratedCacheRead("),
    "model provenance not recorded": (
        "model", "\t\tcacheModel->SetGeneratedCacheContent( true );\n", ""),
    "MD5 purge keeps provenance": (
        "md5", "\tviewEnt = NULL;\n\tgeneratedCacheContent = false;\n", "\tviewEnt = NULL;\n"),
    "MD5R conversion drops provenance": (
        "md5r", "\t// converted data is only as trustworthy as the model it came from\n\tSetGeneratedCacheContent( sourceModel.HoldsGeneratedCacheContent() );\n\n#ifndef", "\n#ifndef"),
    "model purge honours a forged reloadable flag": (
        "model_manager", "if ( ( com_purgeAll.GetBool() && model->IsReloadable() ) || cacheContent ) {",
        "if ( ( com_purgeAll.GetBool() || cacheContent ) && model->IsReloadable() ) {"),
    "cached world retained under a lock": (
        "world", "if ( name == mapName && !retainedWorldRefused ) {", "if ( name == mapName ) {"),
    "world provenance not recorded": (
        "world", "\t\t\t\tworldFromGeneratedCache = true;\n", ""),
    "collision purge removed": (
        "cm_load", "\t\tconst int purged = PurgeGeneratedCacheModels();\n", "\t\tconst int purged = 0;\n"),
    "collision provenance not recorded": (
        "cm_files", "\t\tmodel->fromGeneratedCache = true;\n", ""),
    "single-player animation reader skips the gate": (
        "anim_sp", "\tif ( !fileSystem->GeneratedCacheReadsEnabled( GENERATED_CACHE_ANIMATION ) ) {\n\t\treturn false;\n\t}\n\tidStr cachePath;",
        "\tidStr cachePath;"),
    "multiplayer animations not reloaded at map load": (
        "game_mp", "\t\tconst int reloadedAnims = animationLib->ReloadGeneratedCacheAnims();\n", "\t\tconst int reloadedAnims = 0;\n"),
    "animation reload ignores provenance": (
        "anim_mp", "\t\tif ( animptr == NULL || *animptr == NULL || !( *animptr )->FromGeneratedCache() ) {",
        "\t\tif ( animptr == NULL || *animptr == NULL ) {"),
}


# The check that owns each mutated link, so a mutation cannot pass by tripping
# an unrelated pin.
MUTATION_OWNERS: dict[str, str] = {
    "pure lock no longer refuses reads": "check_gates",
    "fs_restrict no longer refuses reads": "check_gates",
    "read gate skips the refusal": "check_gates",
    "writes continue under a pure lock": "check_gates",
    "coordinator writes the animation kind": "check_gates",
    "animation reads ignore com_levelLoadModernization": "check_gates",
    "filesystem wrapper skips its gate": "check_reads",
    "model provenance not recorded": "check_render_models",
    "MD5 purge keeps provenance": "check_render_models",
    "MD5R conversion drops provenance": "check_render_models",
    "model purge honours a forged reloadable flag": "check_render_models",
    "cached world retained under a lock": "check_world",
    "world provenance not recorded": "check_world",
    "collision purge removed": "check_collision",
    "collision provenance not recorded": "check_collision",
    "single-player animation reader skips the gate": "check_animations",
    "multiplayer animations not reloaded at map load": "check_animations",
    "animation reload ignores provenance": "check_animations",
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--mutations", action="store_true", help="also require each broken link to fail the checks")
    args = parser.parse_args()
    sources = {key: read(relative) for key, relative in SOURCES.items()}
    run_checks(sources)
    if args.mutations:
        for name, (key, old, new) in MUTATIONS.items():
            if sources[key].count(old) != 1:
                raise AssertionError(f"mutation {name!r} no longer matches {SOURCES[key]} exactly once")
            mutated = dict(sources)
            mutated[key] = sources[key].replace(old, new)
            caught, owner = first_failure(mutated), MUTATION_OWNERS[name]
            if caught != owner:
                raise AssertionError(f"mutation {name!r} should fail {owner}, but "
                                     + (f"{caught} failed first" if caught else "nothing failed"))
        print(f"pure_level_load_cache_contract: {len(MUTATIONS)} mutations detected")
    print("pure_level_load_cache_contract: PASS (level-load caches refused while pure, residents re-derived)")


if __name__ == "__main__":
    main()
