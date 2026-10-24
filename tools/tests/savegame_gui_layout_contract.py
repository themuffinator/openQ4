#!/usr/bin/env python3
"""Saved GUI layouts are part of the savegame wire format.

A saved GUI's window tree is positional. Its bytes follow the windows, defined
variables, scripts and timeline events parsed from the .gui file, and nothing in
the payload records which definition that was. Editing a GUI that the game
serializes therefore changes the byte stream exactly as adding a C++ field does,
but neither the payload version nor the source hash notices. 0.14.000 edited
both player HUDs and broke every earlier save's restore after map teardown.

src/framework/SaveGameReleases.h keeps such a GUI's previous revision under
guis/savecompat/ and reads older records through it. This contract makes that
the only way a serialized GUI can change:

* every openQ4 GUI is classified as serialized or never serialized;
* each serialized GUI's current definition matches its pin, or a recorded
  layout-neutral revision of it;
* each revision in OPENQ4_SAVEGAME_GUI_REVISIONS keeps exactly the definition
  the older builds shipped (its explanatory header aside), and its marker window
  exists only in the current definition, which is how the reader tells the two
  records apart; and
* both game libraries read such records through the engine migration, which
  carries changed window values (a visible boss bar) as well as GUI state.

Changing a pinned GUI fails here until it is classified: prove the window tree
is unchanged and record it as layout-neutral, or keep the previous definition as
a new revision and pin the new one.
"""

from __future__ import annotations

import hashlib
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PAK0 = ROOT / "content" / "baseoq4" / "pak0"
REVISION_ROOT = "guis/savecompat"

# Git blob ids (LF-normalized) of every openQ4 GUI a save writes: player HUDs,
# objective and cinematic GUIs, weapon and vehicle zoom GUIs and vehicle HUDs.
SERIALIZED_GUIS: dict[str, str] = {
    "guis/cinematic.gui": "63d5f187afe545a3533f14780605a7e614463f5c",
    "guis/hud.gui": "51c029611142acc27f7e0e03c2f09791d108994e",
    "guis/hud_strogg.gui": "98430b36f89e9b2887eff70d49980178cca8a0f8",
    "guis/vehicles/airdef_cannon.gui": "e92ae101f581b82a2885ac970b8755a92e1cbcae",
    "guis/vehicles/hud.gui": "fa96e0421d400e4e880236fe27b623bdfd2e7e2e",
    "guis/vehicles/strogg_noise.gui": "d0777427b93ec45c90c6dfdf3ba6352aec981dc6",
    "guis/weapons/machinegun_scope.gui": "d2690deda0fc05b110508071a34051631df8c1c9",
    "guis/weapons/nailgun_scope.gui": "ae6876c4199d6d252ea11d80ce284bc6d145d34a",
    "guis/weapons/railgun_scope.gui": "baaa7a75e8daac09597f201c6836a54dafd19f9b",
    "guis/wristcomm.gui": "1f4d8e60cfcd56a46c86b1226240f7b097f30f37",
    "guis/wristcomm_strogg.gui": "226aeadf3791732aaf07b8793edb387ade4be03b",
}

# Shipped revisions proven to keep a pinned window tree. Each maps the shipped
# blob to the pinned blob whose layout it keeps, and the evidence. Add one only
# when the edit cannot add, remove or reorder anything the window tree writes.
LAYOUT_NEUTRAL_EDITS: dict[str, dict[str, tuple[str, str]]] = {
    "guis/weapons/machinegun_scope.gui": {
        "64d0d8cf9310139e82e2426d4f9ebddc16b6ed91": (
            "d2690deda0fc05b110508071a34051631df8c1c9",
            "2126a232 drops matscaley from two windows; it sets the fixed idWindow::matScaley "
            "member, which every window writes whether or not the .gui names it",
        ),
    },
}

# OPENQ4_SAVEGAME_GUI_REVISIONS as it must read, with the blob each saved
# definition keeps and the commit whose parent shipped it.
GUI_REVISIONS: dict[str, dict[str, object]] = {
    "guis/hud.gui": {
        "firstBuild": 1562,
        "savedDefinition": "guis/savecompat/hud_r1.gui",
        "marker": "hit_indicator_reduced",
        "savedBlob": "d403389d3b651fb9dd536bae1cbfe41e950c6f3c",
        "shippedBefore": "38bdc687",
    },
    "guis/hud_strogg.gui": {
        "firstBuild": 1562,
        "savedDefinition": "guis/savecompat/hud_strogg_r1.gui",
        "marker": "hit_indicator_reduced",
        "savedBlob": "cfea8778e2824f7b04348c3bc1f63e6d2fa530bc",
        "shippedBefore": "38bdc687",
    },
}

# openQ4 GUIs that single-player saves never contain: menus, loading screens and
# multiplayer-only GUIs (idPlayer deliberately skips mphud).
NEVER_SERIALIZED_GUIS = {
    "guis/arena_menu.gui",
    "guis/arena_pause.gui",
    "guis/buymenu.gui",
    "guis/campaign_menu.gui",
    "guis/demo_menu.gui",
    "guis/gameover.gui",
    "guis/level_editor_experimental.gui",
    "guis/loading/generic.gui",
    "guis/loading/intro.gui",
    "guis/loading/mplevel.gui",
    "guis/mainmenu.gui",
    "guis/matchcontrol.gui",
    "guis/menu/settings/audio.gui",
    "guis/menu/settings/controls.gui",
    "guis/menu/settings/game.gui",
    "guis/menu/settings/game_hovers.gui",
    "guis/menu/settings/popups.gui",
    "guis/menu/settings/system.gui",
    "guis/mphud.gui",
    "guis/mpmain.gui",
    "guis/mpmsgmode.gui",
    "guis/scoreboard.gui",
    "guis/summary.gui",
}

REVISION_ENTRY = re.compile(
    r'\{\s*"(guis/[^"]+\.gui)"\s*,\s*(\d+)\s*,\s*"(guis/savecompat/[^"]+\.gui)"\s*,\s*"([^"]+)"\s*\}'
)


def git_blob_id(data: bytes) -> str:
    data = data.replace(b"\r\n", b"\n")
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def strip_revision_header(data: bytes) -> bytes:
    """Drop the leading // lines that explain why a saved revision exists."""
    lines = data.replace(b"\r\n", b"\n").split(b"\n")
    index = 0
    while index < len(lines) and lines[index].startswith(b"//"):
        index += 1
    if index and index < len(lines) and lines[index] == b"":
        index += 1
    return b"\n".join(lines[index:])


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"Required source file not found: {path}")
    return path.read_text(encoding="utf-8")


def shipped_guis() -> dict[str, Path]:
    guis: dict[str, Path] = {}
    for pak in sorted((ROOT / "content" / "baseoq4").glob("pak*")):
        for path in pak.glob("guis/**/*.gui"):
            relative = path.relative_to(pak).as_posix()
            if relative.startswith(REVISION_ROOT + "/"):
                continue
            if relative in guis:
                raise AssertionError(f"{relative} ships in more than one pak")
            guis[relative] = path
    return guis


def window_names(text: str) -> set[str]:
    return {name.lower() for name in re.findall(r"windowDef\s+([A-Za-z0-9_]+)", text)}


def validate_classification(guis: dict[str, Path]) -> None:
    serialized = set(SERIALIZED_GUIS)
    overlap = serialized & NEVER_SERIALIZED_GUIS
    if overlap:
        raise AssertionError(f"GUIs classified both ways: {sorted(overlap)}")
    unclassified = sorted(set(guis) - serialized - NEVER_SERIALIZED_GUIS)
    if unclassified:
        raise AssertionError(
            "Unclassified openQ4 GUI(s) "
            + ", ".join(unclassified)
            + ": add each to SERIALIZED_GUIS if any save writes it (player, weapon, vehicle "
            "or entity GUI), otherwise to NEVER_SERIALIZED_GUIS"
        )
    missing = sorted((serialized | NEVER_SERIALIZED_GUIS) - set(guis))
    if missing:
        raise AssertionError(f"Classified GUI(s) no longer ship: {missing}")


def validate_current_pins(guis: dict[str, Path]) -> None:
    for gui, pin in sorted(SERIALIZED_GUIS.items()):
        blob = git_blob_id(guis[gui].read_bytes())
        neutral = LAYOUT_NEUTRAL_EDITS.get(gui, {}).get(blob)
        if blob == pin or (neutral is not None and neutral[0] == pin):
            continue
        raise AssertionError(
            f"{gui} changed (blob {blob}, pinned {pin}). Its window tree is serialized positionally, "
            "so this edit can move every later byte of a save written before it. If it adds, removes "
            "or reorders windows, defined vars, scripts, script statements with owned values, timeline "
            f"or named events, keep the pinned definition under {REVISION_ROOT}/ as a new revision in "
            "OPENQ4_SAVEGAME_GUI_REVISIONS and pin the new one. Record it as layout-neutral only after "
            "proving the window tree is unchanged."
        )


def validate_revisions(guis: dict[str, Path]) -> None:
    releases = read(ROOT / "src" / "framework" / "SaveGameReleases.h")
    match = re.search(r"OPENQ4_SAVEGAME_GUI_REVISIONS\[\]\s*=\s*\{(?P<body>.*?)\n\};", releases, re.DOTALL)
    if match is None:
        raise AssertionError("SaveGameReleases.h has no OPENQ4_SAVEGAME_GUI_REVISIONS table")
    table = {
        gui: {"firstBuild": int(build), "savedDefinition": saved, "marker": marker}
        for gui, build, saved, marker in REVISION_ENTRY.findall(match.group("body"))
    }
    expected = {
        gui: {key: entry[key] for key in ("firstBuild", "savedDefinition", "marker")}
        for gui, entry in GUI_REVISIONS.items()
    }
    if table != expected:
        raise AssertionError(f"OPENQ4_SAVEGAME_GUI_REVISIONS {table} does not match the contract {expected}")

    for gui, entry in sorted(GUI_REVISIONS.items()):
        if gui not in SERIALIZED_GUIS:
            raise AssertionError(f"{gui} has a saved revision but is not classified as serialized")
        saved_path = PAK0 / str(entry["savedDefinition"])
        if not saved_path.is_file():
            raise AssertionError(f"{entry['savedDefinition']} is missing; saves written before build "
                                 f"{entry['firstBuild']} could not restore {gui}")
        saved_blob = git_blob_id(strip_revision_header(saved_path.read_bytes()))
        if saved_blob != entry["savedBlob"]:
            raise AssertionError(f"{entry['savedDefinition']} is {saved_blob} without its header, not the "
                                 f"definition earlier builds shipped ({entry['savedBlob']})")
        marker = str(entry["marker"]).lower()
        if marker not in window_names(guis[gui].read_text(encoding="latin-1")):
            raise AssertionError(f"{gui} no longer has marker window '{entry['marker']}'; the reader could "
                                 "not tell its records from the earlier revision's")
        if marker in window_names(saved_path.read_text(encoding="latin-1")):
            raise AssertionError(f"{entry['savedDefinition']} also has marker window '{entry['marker']}'")

    shipped = {path.relative_to(PAK0).as_posix() for path in (PAK0 / REVISION_ROOT).glob("**/*.gui")}
    orphaned = shipped - {str(entry["savedDefinition"]) for entry in GUI_REVISIONS.values()}
    if orphaned:
        raise AssertionError(f"Saved GUI revisions no reader uses: {sorted(orphaned)}")


def validate_release_provenance() -> None:
    """Optional: each saved revision is what the commit before its change shipped."""
    for gui, entry in sorted(GUI_REVISIONS.items()):
        result = subprocess.run(
            ["git", "-C", str(ROOT), "rev-parse", "--verify", "--quiet",
             f"{entry['shippedBefore']}^:content/baseoq4/pak0/{gui}"],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print("savegame_gui_layout_contract: history not available; skipped revision provenance")
            return
        if result.stdout.strip() != entry["savedBlob"]:
            raise AssertionError(f"{gui} revision pin {entry['savedBlob']} is not what "
                                 f"{entry['shippedBefore']}^ shipped ({result.stdout.strip()})")


def validate_engine_migration() -> None:
    for tree, context in (("game", "SP GameLib"), ("mpgame", "MP GameLib")):
        source = read(ROOT / "src" / tree / "gamesys" / "SaveGame.cpp")
        start = source.find("void idRestoreGame::ReadUserInterfaceRevision(")
        if start < 0:
            raise AssertionError(f"{context} has no GUI revision reader")
        body = source[start:source.index("\n}\n", start)]
        # Copying only the state dictionary loses window values scripts set,
        # such as a visible boss bar; the engine migration carries those too.
        if "ui->ReadFromSaveGameWithLayout( file, revision.savedDefinition, restoredWindowValues )" not in body:
            raise AssertionError(f"{context} does not read earlier GUI revisions through the engine window migration")
        if "SetStateString" in body:
            raise AssertionError(f"{context} still copies GUI state by hand beside the engine migration")


def validate_eval_transfer() -> None:
    """A carried window value keeps whether it still follows its expression."""
    source = read(ROOT / "src" / "ui" / "UserInterface.cpp")
    start = source.find("bool idUserInterfaceLocal::ReadFromSaveGameWithLayout(")
    if start < 0:
        raise AssertionError("idUserInterfaceLocal::ReadFromSaveGameWithLayout is missing")
    body = source[start:source.index("\n}\n", start)]
    # A script `set` clears eval so the expression stops rewriting the value;
    # without it the next GUI update recomputes a hidden panel back into view.
    for token in (
        "savedVar->GetEval() != initialVar->GetEval()",
        "if ( !valueChanged && !evalChanged )",
        "liveVar->SetEval( savedVar->GetEval() );",
    ):
        if token not in body:
            raise AssertionError(f"GUI revision migration lacks {token!r}; restored overrides would be re-evaluated")


def main() -> None:
    guis = shipped_guis()
    validate_classification(guis)
    validate_current_pins(guis)
    validate_revisions(guis)
    validate_release_provenance()
    validate_engine_migration()
    validate_eval_transfer()
    print("savegame_gui_layout_contract: ok")


if __name__ == "__main__":
    main()
