#!/usr/bin/env python3
"""Execute the retained HUD's artwork catalog and the game's HUD presentation precache (MP-07).

Every retained HUD builds its view from guis/hud/artwork.rui, 8 MB of vector art: each
stock HUD GUI, and each source the game draws itself (native/crosshair, native/hit-marker,
native/damage-numbers). Each HUD used to read and parse that file on its first draw, about
200 ms on a debugoptimized build, so the first damage number of a match held the client's
frame mid-fight. A client sends the server no commands while its frame stalls: the server
repeated the last one, so its copy of the client's aim stood still while the client turned,
and after MAX_USERCMD_DUPLICATION repeats it dropped the attack.

tools/tests/native/RetainedHudCatalogFixture.cpp executes, spliced verbatim from production:
the level's catalog, idRetainedHud::Impl and idRetainedHud::Prepare and the UI manager's
GeneratedHud and PrecacheHudPrimitives (src/ui/RetainedHud.cpp), and both games'
idPlayer::PrecacheHudPresentation. A level load parses the artwork once, on a worker; every
HUD builds its view from that parse; a failure is reported per HUD without reading the
files again; the local player builds the crosshair, hit marker and damage number views as it
spawns. The source checks pin the wiring around it.

Single-reversal controls re-run the checks against mutated production source and require
each one to fail. The first restores the defect: every HUD parses the artwork again.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from mp_lag_compensation_state import block

ROOT = Path(__file__).resolve().parents[2]

# Each reverses one agreement the fixture executes; every one must fail it.
STATE_MUTATIONS = (
    ("every HUD parses the artwork again (the 13 November defect)", "hud",
     "\tif (catalog) return catalog;\n", "\tif (false) return catalog;\n"),
    ("a HUD's view parses the source itself", "hud",
     "RetainedUI_LoadParsedView(impl->view,impl->catalog->artwork,Artwork,diagnostics)",
     "RetainedUI_LoadView(impl->view,impl->catalog->artwork.Source(),Artwork,diagnostics)"),
    ("a level keeps the last level's catalog", "hud", "\tcatalog.reset();\n", "\t(void)0;\n"),
    ("the artwork is not parsed during the level load", "hud",
     "if (cvarSystem->GetCVarBool(\"ui_retained\")) UI_RetainedPrefetchDocuments(&Artwork,1);", "(void)0;"),
    ("the stock interface parses the artwork too", "hud",
     "if (cvarSystem->GetCVarBool(\"ui_retained\")) UI_RetainedPrefetchDocuments(&Artwork,1);",
     "UI_RetainedPrefetchDocuments(&Artwork,1);"),
    ("a worker's parse is ignored", "hud",
     "loaded->prefetched=UI_RetainedTakePrefetched(Artwork,static_cast<int>(artwork.size()),prefetchedSource,prefetchedStamp,loaded->artwork);", "loaded->prefetched=false;"),
    ("a failed catalog is read again by each HUD", "hud", "\tcatalog=loaded;\n\treturn catalog;\n",
     "\tif (loaded->error.empty()) catalog=loaded;\n\treturn loaded;\n"),
    ("a prepared HUD builds another view", "hud", "if (impl->attempted) return impl->view!=nullptr;",
     "if (false) return impl->view!=nullptr;"),
    ("a mod's own HUD loads the catalog", "hud",
     "\tif (idLegacyHudLayout::ModOwns(source)) return false;\n\tconst int started", "\tconst int started"),
    ("precaching a source builds nothing", "hud", "if (idRetainedHud* hud=GeneratedHud(source)) hud->Prepare(source);",
     "(void)GeneratedHud(source);"),
    ("multiplayer: the damage numbers are not precached", "mp_presentation",
     "if ( hud_damageNumbers.GetInteger() > DAMAGENUMBERS_OFF ) {", "if ( false ) {"),
    ("multiplayer: every player precaches", "mp_presentation",
     "entityNumber != gameLocal.localClientNum ) return;", "false ) return;"),
    ("multiplayer: the stock interface precaches", "mp_presentation",
     "if ( !cvarSystem->GetCVarBool( \"ui_retained\" ) ) return;", ""),
    ("multiplayer: the hit marker beside the animated crosshair", "mp_presentation",
     "} else if ( hud_hitMarker.GetBool() ) {", "} if ( hud_hitMarker.GetBool() ) {"),
    ("single player: the hit marker beside the animated crosshair", "sp_presentation",
     "} else if ( hud_hitMarker.GetBool() ) {", "} if ( hud_hitMarker.GetBool() ) {"),
)


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8").replace("\r\n", "\n")


def presentation(player: str) -> str:
    return block(player, "void idPlayer::PrecacheHudPresentation( void ) {")


def build_fixture(hud: str, mp_presentation: str, sp_presentation: str) -> str:
    harness = (ROOT / "tools/tests/native/RetainedHudCatalogFixture.cpp").read_text(encoding="utf-8")
    variable = "std::shared_ptr<const Catalog> catalog;"
    if variable not in hud:
        raise AssertionError("the catalog variable is missing")
    parts = {
        "// PRODUCTION_CATALOG_STRUCT": block(hud, "struct Catalog {") + ";",
        "// PRODUCTION_CATALOG_VARIABLE": variable,
        "// PRODUCTION_SHARED_CATALOG": block(hud, "std::shared_ptr<const Catalog> SharedCatalog() {"),
        "// PRODUCTION_PREFETCH_CATALOG": block(hud, "void PrefetchCatalog() {"),
        "// PRODUCTION_IMPL": block(hud, "struct idRetainedHud::Impl {") + ";",
        "// PRODUCTION_PREPARE": block(hud, "bool idRetainedHud::Prepare(const char* source) {"),
        "// PRODUCTION_GENERATED_HUD": block(hud, "idRetainedHud* idUserInterfaceManagerLocal::GeneratedHud(const char* source) {"),
        "// PRODUCTION_PRECACHE_PRIMITIVES": block(hud, "void idUserInterfaceManagerLocal::PrecacheHudPrimitives(const char* source) {"),
        "// PRODUCTION_MP_PRESENTATION": mp_presentation,
        "// PRODUCTION_SP_PRESENTATION": sp_presentation,
    }
    for marker, text in parts.items():
        harness = harness.replace(marker, text)
    return harness


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"{context}: missing {token!r}")


def check_sources(sources: dict[str, str]) -> None:
    hud, mp, sp, interface = sources["hud"], sources["mp_player"], sources["sp_player"], sources["interface"]
    precache = block(hud, "void idRetainedHud::Precache() {")
    if not precache.split("{", 1)[1].lstrip().startswith("PrefetchCatalog();"):
        raise AssertionError("idRetainedHud::Precache: a level load must drop the catalog first, whatever the gate says")
    draw = block(hud, "bool idRetainedHud::Draw(const char* source,std::vector<openq4::ui::HudPrimitive>&& captured) {")
    require(draw, "if (!Prepare(source) || !RetainedUI_PrepareView(impl->view)) return false;", "idRetainedHud::Draw")
    require(block(hud, "bool idUserInterfaceManagerLocal::DrawHudPrimitives("), "idRetainedHud* hud=GeneratedHud(source);",
            "DrawHudPrimitives")
    for name, player, follows in (("multiplayer", mp, "PrecacheQuickChatGui();"), ("single player", sp, "PrecacheWeaponWheelGui();")):
        spawn = block(player, "void idPlayer::Spawn( void ) {")
        require(spawn, f"{follows}\n\tPrecacheHudPresentation();", f"{name} idPlayer::Spawn")
    require(interface, "virtual void PrecacheHudPrimitives( const char* source ) { }", "idUserInterfaceManager")


def compile_and_run(source: str, directory: Path, name: str, compiler: str) -> subprocess.CompletedProcess:
    cpp = directory / f"{name}.cpp"
    exe = directory / (f"{name}.exe" if os.name == "nt" else name)
    cpp.write_text(source, encoding="utf-8")
    command = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-unused-variable",
               "-Wno-unused-const-variable", str(cpp), "-o", str(exe)]
    if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, cwd=directory, check=True)
    try:
        return subprocess.run([str(exe)], cwd=directory, capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired as expired:
        return subprocess.CompletedProcess(expired.cmd, 1, expired.stdout or "", "timed out")


def main() -> None:
    sources = {"hud": read("src/ui/RetainedHud.cpp"), "mp_player": read("src/mpgame/Player.cpp"),
               "sp_player": read("src/game/Player.cpp"), "interface": read("src/ui/UserInterface.h")}
    check_sources(sources)

    # The source checks must reject each wiring reversal.
    source_controls = (
        ("a level load keeps the catalog", "hud", "void idRetainedHud::Precache() {\n\tPrefetchCatalog();\n",
         "void idRetainedHud::Precache() {\n"),
        ("a draw loads without Prepare", "hud", "if (!Prepare(source) || !RetainedUI_PrepareView(impl->view)) return false;",
         "if (!RetainedUI_PrepareView(impl->view)) return false;"),
        ("multiplayer spawns without precaching", "mp_player", "PrecacheQuickChatGui();\n\tPrecacheHudPresentation();",
         "PrecacheQuickChatGui();"),
        ("single player spawns without precaching", "sp_player", "PrecacheWeaponWheelGui();\n\tPrecacheHudPresentation();",
         "PrecacheWeaponWheelGui();"),
        ("the manager interface cannot precache", "interface",
         "virtual void PrecacheHudPrimitives( const char* source ) { }", ""),
    )
    for label, name, original, mutated in source_controls:
        if sources[name].count(original) != 1:
            raise SystemExit(f"ui_hud_catalog_state: source control anchor not unique: {label}")
        try:
            check_sources(dict(sources, **{name: sources[name].replace(original, mutated, 1)}))
        except AssertionError:
            continue
        raise SystemExit(f"ui_hud_catalog_state: source control did not fail: {label}")

    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for the retained HUD catalog fixture")
    parts = {"hud": sources["hud"], "mp_presentation": presentation(sources["mp_player"]),
             "sp_presentation": presentation(sources["sp_player"])}
    output = ROOT / ".tmp"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ui-hud-catalog-", dir=output) as directory:
        path = Path(directory)
        result = compile_and_run(build_fixture(**parts), path, "catalog", compiler)
        if result.returncode != 0:
            raise SystemExit(f"ui_hud_catalog_state: FAILED\n{result.stdout}{result.stderr}")
        print(result.stdout.strip())
        for index, (label, name, original, mutated) in enumerate(STATE_MUTATIONS):
            if parts[name].count(original) != 1:
                raise SystemExit(f"ui_hud_catalog_state: mutation anchor not unique: {label}")
            try:
                source = build_fixture(**dict(parts, **{name: parts[name].replace(original, mutated, 1)}))
            except (AssertionError, ValueError):
                continue
            control = compile_and_run(source, path, f"control{index}", compiler)
            if control.returncode == 0:
                raise SystemExit(f"ui_hud_catalog_state: control did not fail: {label}\n{control.stdout}")

    controls = len(source_controls) + len(STATE_MUTATIONS)
    print(f"ui_hud_catalog_state: ok ({controls} single-reversal controls fail)")


if __name__ == "__main__":
    main()
