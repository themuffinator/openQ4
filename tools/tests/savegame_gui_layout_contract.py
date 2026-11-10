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
    "guis/hud.gui": "79d25f0c7b5035f118655e1f9f01db89157e6140",
    "guis/hud_strogg.gui": "51981879a988f53c7b884a84036b6ef66968a37a",
    # The Awakening's guis/q4xhud.gui, which the engine reads from this definition
    # (idLegacyHudLayout::DefinitionFor; generated from guis/hud.gui).
    "guis/openq4/awakening/q4xhud.gui": "56a23a1fc2515b0ade85217725d68684d0f8bde6",
    "guis/openq4/awakening/cannonplayerhud.gui": "c9534629a60ffedf211207d5f979c7e8c5ecf598",
    "guis/openq4/awakening/speederplayerhud.gui": "91ce8efaf287ba39fa72ad03b77173142609befe",
    "guis/openq4/awakening/turretplayerhud.gui": "e108d7a8aa51171cea6f77e9b923d629d42aad44",
    "guis/vehicles/airdef_cannon.gui": "e92ae101f581b82a2885ac970b8755a92e1cbcae",
    "guis/vehicles/hud.gui": "fcea89c5dd4a1a0b0afcb46ae3cdd4ea2beb42b1",
    "guis/vehicles/strogg_noise.gui": "d0777427b93ec45c90c6dfdf3ba6352aec981dc6",
    "guis/vehicles/turret_hud.gui": "7da86966bf5b66f3e5c90f926cf0a739eaf231e4",
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

# OPENQ4_SAVEGAME_GUI_REVISIONS as it must read, in table order (a GUI edited
# more than once lists its revisions oldest first), with the blob each saved
# definition keeps and a commit that shipped it: `shippedBefore` names the
# commit that changed it (its parent shipped it), `shippedIn` one that shipped it.
GUI_REVISIONS: list[dict[str, object]] = [
    {
        "gui": "guis/hud.gui",
        "firstBuild": 1562,
        "savedDefinition": "guis/savecompat/hud_r1.gui",
        "marker": "hit_indicator_reduced",
        "savedBlob": "d403389d3b651fb9dd536bae1cbfe41e950c6f3c",
        "shippedBefore": "38bdc687",
    },
    {
        "gui": "guis/hud_strogg.gui",
        "firstBuild": 1562,
        "savedDefinition": "guis/savecompat/hud_strogg_r1.gui",
        "marker": "hit_indicator_reduced",
        "savedBlob": "cfea8778e2824f7b04348c3bc1f63e6d2fa530bc",
        "shippedBefore": "38bdc687",
    },
    {
        "gui": "guis/hud.gui",
        "firstBuild": 2280,
        "savedDefinition": "guis/savecompat/hud_r2.gui",
        "marker": "p_pickup0",
        "savedBlob": "51c029611142acc27f7e0e03c2f09791d108994e",
        "shippedIn": "8fdd8f077",
    },
    {
        "gui": "guis/hud_strogg.gui",
        "firstBuild": 2280,
        "savedDefinition": "guis/savecompat/hud_strogg_r2.gui",
        "marker": "p_pickup0",
        "savedBlob": "98430b36f89e9b2887eff70d49980178cca8a0f8",
        "shippedIn": "8fdd8f077",
    },
    {
        "gui": "guis/vehicles/hud.gui",
        "firstBuild": 2280,
        "savedDefinition": "guis/savecompat/vehicle_hud_r1.gui",
        "marker": "oq4_prompt",
        "savedBlob": "fa96e0421d400e4e880236fe27b623bdfd2e7e2e",
        "shippedIn": "8fdd8f077",
    },
    {
        "gui": "guis/hud.gui",
        "firstBuild": 2323,
        "savedDefinition": "guis/savecompat/hud_r3.gui",
        "marker": "hit_chevron",
        "savedBlob": "9d918517cc81194d1f9798ccd92ab566d8b4799d",
        "shippedIn": "9cc5e67a4",
    },
    {
        "gui": "guis/hud_strogg.gui",
        "firstBuild": 2323,
        "savedDefinition": "guis/savecompat/hud_strogg_r3.gui",
        "marker": "hit_chevron",
        "savedBlob": "de8ce88cbf1444b424ae27dd093f0d33f30a1144",
        "shippedIn": "9cc5e67a4",
    },
    {
        "gui": "guis/vehicles/hud.gui",
        "firstBuild": 2323,
        "savedDefinition": "guis/savecompat/vehicle_hud_r2.gui",
        "marker": "oq4_prompt_padlock",
        "savedBlob": "95493b94ce52d5ff87683c94e2b09a318bc5f3e9",
        "shippedIn": "9cc5e67a4",
    },
    # A GUI another game ships, read from openQ4's own definition of it: `definition`
    # is that file, and the saved definition is the game's own file (`external`),
    # which openQ4 does not ship and the engine parses as written.
    {
        "gui": "guis/q4xhud.gui",
        "firstBuild": 2330,
        "savedDefinition": "guis/q4xhud.gui",
        "marker": "p_pickup0",
        "definition": "guis/openq4/awakening/q4xhud.gui",
        "external": "The Awakening (q4xbase)",
    },
    {
        "gui": "guis/vehicles/airassault/turretplayerhud.gui",
        "firstBuild": 2334,
        "savedDefinition": "guis/vehicles/airassault/turretplayerhud.gui",
        "marker": "oq4_prompt",
        "definition": "guis/openq4/awakening/turretplayerhud.gui",
        "external": "The Awakening (q4xbase)",
    },
    {
        "gui": "guis/vehicles/mcc/cannonplayerhud.gui",
        "firstBuild": 2334,
        "savedDefinition": "guis/vehicles/mcc/cannonplayerhud.gui",
        "marker": "oq4_prompt",
        "definition": "guis/openq4/awakening/cannonplayerhud.gui",
        "external": "The Awakening (q4xbase)",
    },
    {
        "gui": "guis/vehicles/speederplayerhud.gui",
        "firstBuild": 2334,
        "savedDefinition": "guis/vehicles/speederplayerhud.gui",
        "marker": "oq4_prompt",
        "definition": "guis/openq4/awakening/speederplayerhud.gui",
        "external": "The Awakening (q4xbase)",
    },
    {
        "gui": "guis/vehicles/hud.gui",
        "firstBuild": 2342,
        "savedDefinition": "guis/savecompat/vehicle_hud_r3.gui",
        "marker": "lock_state_msg",
        "savedBlob": "816270928d67d8825d15add0b235727f7f9adb97",
        "shippedIn": "9dc0fecf8",
    },
    {
        "gui": "guis/vehicles/airassault/turretplayerhud.gui",
        "firstBuild": 2342,
        "savedDefinition": "guis/savecompat/q4x_turretplayerhud_r1.gui",
        "marker": "lock_state_msg",
        "savedBlob": "1450369bf59ac72a8f611ad495b60f5f1a8771bd",
        "shippedIn": "9dc0fecf8",
        "definition": "guis/openq4/awakening/turretplayerhud.gui",
    },
    {
        "gui": "guis/vehicles/mcc/cannonplayerhud.gui",
        "firstBuild": 2342,
        "savedDefinition": "guis/savecompat/q4x_cannonplayerhud_r1.gui",
        "marker": "lock_state_msg",
        "savedBlob": "d2ed3e80e9b377b455f3d8232ded903c2de0f6b7",
        "shippedIn": "9dc0fecf8",
        "definition": "guis/openq4/awakening/cannonplayerhud.gui",
    },
    {
        "gui": "guis/vehicles/speederplayerhud.gui",
        "firstBuild": 2342,
        "savedDefinition": "guis/savecompat/q4x_speederplayerhud_r1.gui",
        "marker": "lock_state_msg",
        "savedBlob": "8c51b2aef073eb06156e596cd35b9e18c84dc235",
        "shippedIn": "9dc0fecf8",
        "definition": "guis/openq4/awakening/speederplayerhud.gui",
    },
]

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
    r'\{\s*"(guis/[^"]+\.gui)"\s*,\s*(\d+)\s*,\s*"(guis/[^"]+\.gui)"\s*,\s*"([^"]+)"\s*\}'
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
    table = [
        {"gui": gui, "firstBuild": int(build), "savedDefinition": saved, "marker": marker}
        for gui, build, saved, marker in REVISION_ENTRY.findall(match.group("body"))
    ]
    expected = [
        {key: entry[key] for key in ("gui", "firstBuild", "savedDefinition", "marker")}
        for entry in GUI_REVISIONS
    ]
    if table != expected:
        raise AssertionError(f"OPENQ4_SAVEGAME_GUI_REVISIONS {table} does not match the contract {expected}")
    # The reader takes a GUI's first revision its payload predates.
    for gui in {str(entry["gui"]) for entry in GUI_REVISIONS}:
        builds = [int(entry["firstBuild"]) for entry in GUI_REVISIONS if entry["gui"] == gui]
        if builds != sorted(set(builds)):
            raise AssertionError(f"{gui}'s revisions are not listed oldest first with distinct builds: {builds}")

    for entry in GUI_REVISIONS:
        gui = str(entry.get("definition", entry["gui"]))
        if gui not in SERIALIZED_GUIS:
            raise AssertionError(f"{gui} has a saved revision but is not classified as serialized")
        marker = str(entry["marker"]).lower()
        if "external" in entry:
            # The saved definition is another game's file: the record's name, parsed as written.
            if entry["savedDefinition"] != entry["gui"] or "definition" not in entry:
                raise AssertionError(f"{entry['gui']}: an external saved definition is the game's own file, "
                                     "read through an openQ4 definition")
            if (PAK0 / str(entry["savedDefinition"])).exists():
                raise AssertionError(f"{entry['savedDefinition']} ships with openQ4; keep its revision under {REVISION_ROOT}/")
            if marker not in window_names(guis[gui].read_text(encoding="latin-1")):
                raise AssertionError(f"{gui} no longer has marker window '{entry['marker']}'")
            continue
        if not str(entry["savedDefinition"]).startswith(REVISION_ROOT + "/"):
            raise AssertionError(f"{entry['savedDefinition']} is not under {REVISION_ROOT}/")
        saved_path = PAK0 / str(entry["savedDefinition"])
        if not saved_path.is_file():
            raise AssertionError(f"{entry['savedDefinition']} is missing; saves written before build "
                                 f"{entry['firstBuild']} could not restore {gui}")
        saved_blob = git_blob_id(strip_revision_header(saved_path.read_bytes()))
        if saved_blob != entry["savedBlob"]:
            raise AssertionError(f"{entry['savedDefinition']} is {saved_blob} without its header, not the "
                                 f"definition earlier builds shipped ({entry['savedBlob']})")
        if marker not in window_names(guis[gui].read_text(encoding="latin-1")):
            raise AssertionError(f"{gui} no longer has marker window '{entry['marker']}'; the reader could "
                                 "not tell its records from the earlier revision's")
        if marker in window_names(saved_path.read_text(encoding="latin-1")):
            raise AssertionError(f"{entry['savedDefinition']} also has marker window '{entry['marker']}'")

    shipped = {path.relative_to(PAK0).as_posix() for path in (PAK0 / REVISION_ROOT).glob("**/*.gui")}
    orphaned = shipped - {str(entry["savedDefinition"]) for entry in GUI_REVISIONS}
    if orphaned:
        raise AssertionError(f"Saved GUI revisions no reader uses: {sorted(orphaned)}")


def validate_release_provenance() -> None:
    """Optional: each saved revision is what a commit before its change shipped."""
    for entry in GUI_REVISIONS:
        if "external" in entry:
            continue
        gui = entry.get("definition", entry["gui"])
        shipped = entry["shippedIn"] if "shippedIn" in entry else f"{entry['shippedBefore']}^"
        result = subprocess.run(
            ["git", "-C", str(ROOT), "rev-parse", "--verify", "--quiet",
             f"{shipped}:content/baseoq4/pak0/{gui}"],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print("savegame_gui_layout_contract: history not available; skipped revision provenance")
            return
        if result.stdout.strip() != entry["savedBlob"]:
            raise AssertionError(f"{gui} revision pin {entry['savedBlob']} is not what "
                                 f"{shipped} shipped ({result.stdout.strip()})")


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


def validate_external_definitions() -> None:
    """A GUI read from openQ4's definition restores earlier records through the game's own file."""
    hud = read(ROOT / "src" / "ui" / "RetainedHud.cpp")
    interface = read(ROOT / "src" / "ui" / "UserInterface.cpp")
    for entry in GUI_REVISIONS:
        if "external" not in entry:
            continue
        for token in (f'"{entry["gui"]}"', f'"{entry["definition"]}"'):
            if token not in hud:
                raise AssertionError(f"idLegacyHudLayout::DefinitionFor no longer names {token}")
    for token, why in (
        ("definition = literalDefinition ? qpath : idLegacyHudLayout::DefinitionFor( qpath );",
         "a GUI is parsed from its definition"),
        ("saved.literalDefinition = initial.literalDefinition = true;",
         "a saved definition is parsed as written, or an earlier record would be read through the current definition"),
        ("if ( definition.Icmp( layoutPath ) == 0 ) {",
         "a GUI still parsed from the saved definition reads its record directly"),
    ):
        if token not in interface:
            raise AssertionError(f"UserInterface.cpp lacks {token!r}: {why}")


def main() -> None:
    guis = shipped_guis()
    validate_classification(guis)
    validate_current_pins(guis)
    validate_revisions(guis)
    validate_release_provenance()
    validate_engine_migration()
    validate_eval_transfer()
    validate_external_definitions()
    print("savegame_gui_layout_contract: ok")


if __name__ == "__main__":
    main()
