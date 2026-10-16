#!/usr/bin/env python3
"""Saved GUI layouts are part of the savegame wire format.

A saved GUI's window tree is positional. Its bytes follow the windows, defined
variables, scripts and timeline events parsed from the .gui file, and nothing in
the payload records which definition that was. Editing a GUI that the game
serializes therefore changes the byte stream exactly as adding a C++ field does,
but neither the payload version nor the generated source hash notices.

openQ4 0.14.000 edited both player HUDs in the change that moved the payload to
v4. The v3 decoder handled the new C++ fields and kept parsing the v3 HUD window
tree with the v4 definition, so every v0.10-v0.13.2 save failed part way through
the restore, after the running map had been torn down.

This contract pins each serialized openQ4 GUI per payload version:

* the current definition must match its pin for the current payload version;
* where an older decodable version used a different definition, that exact
  definition must ship under guis/savegame/v<N>/ and both game modules must map
  the GUI to it; and
* every openQ4 GUI must be classified as serialized or never serialized.

Changing a pinned GUI therefore fails here until the change is classified: either
prove it leaves the saved window tree unchanged and repin, or bump the payload
version and keep the previous definition for its decoder.

It also checks that only v3 payloads consult the writer's build number. Build
numbers are commit counts: a source archive stamps 0 and a shallow clone 1.
"""

from __future__ import annotations

import hashlib
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PAK0 = ROOT / "content" / "baseoq4" / "pak0"
LAYOUT_ROOT = "guis/savegame"

# The last release that wrote each older payload version. Used only for an
# optional provenance check when Git history is available.
LAST_RELEASE_FOR_VERSION = {3: "v0.13.2"}

# Git blob ids (LF-normalized) of every openQ4 GUI the game writes into a save,
# by payload version: player HUDs, objective and cinematic GUIs, weapon and
# vehicle zoom GUIs and vehicle HUDs.
SERIALIZED_GUI_LAYOUTS: dict[str, dict[int, str]] = {
    "guis/cinematic.gui": {
        3: "63d5f187afe545a3533f14780605a7e614463f5c",
        4: "63d5f187afe545a3533f14780605a7e614463f5c",
    },
    "guis/hud.gui": {
        3: "d403389d3b651fb9dd536bae1cbfe41e950c6f3c",
        4: "51c029611142acc27f7e0e03c2f09791d108994e",
    },
    "guis/hud_strogg.gui": {
        3: "cfea8778e2824f7b04348c3bc1f63e6d2fa530bc",
        4: "98430b36f89e9b2887eff70d49980178cca8a0f8",
    },
    "guis/vehicles/airdef_cannon.gui": {
        3: "e92ae101f581b82a2885ac970b8755a92e1cbcae",
        4: "e92ae101f581b82a2885ac970b8755a92e1cbcae",
    },
    "guis/vehicles/hud.gui": {
        3: "fa96e0421d400e4e880236fe27b623bdfd2e7e2e",
        4: "fa96e0421d400e4e880236fe27b623bdfd2e7e2e",
    },
    "guis/vehicles/strogg_noise.gui": {
        3: "d0777427b93ec45c90c6dfdf3ba6352aec981dc6",
        4: "d0777427b93ec45c90c6dfdf3ba6352aec981dc6",
    },
    "guis/weapons/machinegun_scope.gui": {
        3: "d2690deda0fc05b110508071a34051631df8c1c9",
        4: "d2690deda0fc05b110508071a34051631df8c1c9",
    },
    "guis/weapons/nailgun_scope.gui": {
        3: "ae6876c4199d6d252ea11d80ce284bc6d145d34a",
        4: "ae6876c4199d6d252ea11d80ce284bc6d145d34a",
    },
    "guis/weapons/railgun_scope.gui": {
        3: "baaa7a75e8daac09597f201c6836a54dafd19f9b",
        4: "baaa7a75e8daac09597f201c6836a54dafd19f9b",
    },
    "guis/wristcomm.gui": {
        3: "1f4d8e60cfcd56a46c86b1226240f7b097f30f37",
        4: "1f4d8e60cfcd56a46c86b1226240f7b097f30f37",
    },
    "guis/wristcomm_strogg.gui": {
        3: "226aeadf3791732aaf07b8793edb387ade4be03b",
        4: "226aeadf3791732aaf07b8793edb387ade4be03b",
    },
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

LAYOUT_TABLE_ENTRY = re.compile(r'\{\s*"(guis/[^"]+\.gui)"\s*,\s*"(guis/savegame/v(\d+)/[^"]+\.gui)"\s*\}')


def git_blob_id(data: bytes) -> str:
    data = data.replace(b"\r\n", b"\n")
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"Required source file not found: {path}")
    return path.read_text(encoding="utf-8")


def constant(source: str, name: str) -> int:
    match = re.search(rf"\b{name}\s*=\s*(\d+)\s*;", source)
    if match is None:
        raise AssertionError(f"Missing integer constant {name}")
    return int(match.group(1))


def shipped_guis() -> dict[str, Path]:
    guis: dict[str, Path] = {}
    for pak in sorted((ROOT / "content" / "baseoq4").glob("pak*")):
        for path in pak.glob("guis/**/*.gui"):
            relative = path.relative_to(pak).as_posix()
            if relative.startswith(LAYOUT_ROOT + "/"):
                continue
            if relative in guis:
                raise AssertionError(f"{relative} ships in more than one pak")
            guis[relative] = path
    return guis


def layout_table(source: str, context: str) -> dict[str, tuple[str, int]]:
    match = re.search(r"openQ4SaveGameV3GuiLayouts\[\]\s*=\s*\{(?P<body>.*?)\n\};", source, re.DOTALL)
    if match is None:
        return {}
    table: dict[str, tuple[str, int]] = {}
    for gui, layout, version in LAYOUT_TABLE_ENTRY.findall(match.group("body")):
        if gui in table:
            raise AssertionError(f"{context} maps {gui} twice")
        table[gui.lower()] = (layout, int(version))
    return table


def validate_classification(guis: dict[str, Path]) -> None:
    serialized = set(SERIALIZED_GUI_LAYOUTS)
    overlap = serialized & NEVER_SERIALIZED_GUIS
    if overlap:
        raise AssertionError(f"GUIs classified both ways: {sorted(overlap)}")
    unclassified = sorted(set(guis) - serialized - NEVER_SERIALIZED_GUIS)
    if unclassified:
        raise AssertionError(
            "Unclassified openQ4 GUI(s) "
            + ", ".join(unclassified)
            + ": add each to SERIALIZED_GUI_LAYOUTS if any save writes it (player, weapon, vehicle "
            "or entity GUI), otherwise to NEVER_SERIALIZED_GUIS"
        )
    missing = sorted((serialized | NEVER_SERIALIZED_GUIS) - set(guis))
    if missing:
        raise AssertionError(f"Classified GUI(s) no longer ship: {missing}")


def validate_layout_pins(guis: dict[str, Path], current: int, previous: int, tables: dict[str, dict]) -> None:
    for gui, pins in sorted(SERIALIZED_GUI_LAYOUTS.items()):
        if set(pins) != {previous, current}:
            raise AssertionError(
                f"{gui} must be pinned for payload v{previous} and v{current}, got {sorted(pins)}; "
                "a payload bump must pin the new version and keep the decoded one"
            )
        blob = git_blob_id(guis[gui].read_bytes())
        neutral = LAYOUT_NEUTRAL_EDITS.get(gui, {}).get(blob)
        if neutral is not None and neutral[0] == pins[current]:
            continue
        if blob != pins[current]:
            raise AssertionError(
                f"{gui} changed (blob {blob}, pinned {pins[current]} for payload v{current}). Its window tree "
                "is serialized positionally, so this edit can move every later byte of a save. If it adds, "
                "removes or reorders windows, defined vars, scripts, script statements with owned values, "
                "timeline or named events, bump the payload version and keep the old definition under "
                f"{LAYOUT_ROOT}/v{current}/ for its decoder. Repin only after proving the window tree is unchanged."
            )

    expected: dict[str, tuple[str, int]] = {}
    for gui, pins in sorted(SERIALIZED_GUI_LAYOUTS.items()):
        if pins[previous] == pins[current]:
            continue
        layout = f"{LAYOUT_ROOT}/v{previous}/{gui[len('guis/'):]}"
        layout_path = PAK0 / layout
        if not layout_path.is_file():
            raise AssertionError(
                f"Payload v{previous} saves contain {gui} as pinned {pins[previous]}, but {layout} is missing; "
                f"the v{previous} decoder would parse their window tree with the v{current} definition"
            )
        layout_blob = git_blob_id(layout_path.read_bytes())
        if layout_blob != pins[previous]:
            raise AssertionError(f"{layout} is {layout_blob}, not the pinned v{previous} definition {pins[previous]}")
        expected[gui] = (layout, previous)

    for context, table in tables.items():
        if table != expected:
            raise AssertionError(f"{context} v{previous} GUI layout table {table} does not match the pins {expected}")

    shipped_layouts = {
        path.relative_to(PAK0).as_posix() for path in (PAK0 / LAYOUT_ROOT).glob("**/*.gui")
    } if (PAK0 / LAYOUT_ROOT).is_dir() else set()
    orphaned = shipped_layouts - {layout for layout, _ in expected.values()}
    if orphaned:
        raise AssertionError(f"Saved-GUI layouts no decoder uses: {sorted(orphaned)}")


def validate_release_provenance(previous: int) -> None:
    """Optional: the decoder's pins must be what the last such release shipped."""
    tag = LAST_RELEASE_FOR_VERSION.get(previous)
    if tag is None:
        raise AssertionError(f"No last-release tag recorded for payload v{previous}")
    for gui, pins in sorted(SERIALIZED_GUI_LAYOUTS.items()):
        result = subprocess.run(
            ["git", "-C", str(ROOT), "rev-parse", "--verify", "--quiet", f"{tag}:content/baseoq4/pak0/{gui}"],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print(f"savegame_gui_layout_contract: {tag} not available; skipped release provenance")
            return
        if result.stdout.strip() != pins[previous]:
            raise AssertionError(f"{gui} v{previous} pin {pins[previous]} is not what {tag} shipped ({result.stdout.strip()})")


def validate_build_number_scope(session: str, games: dict[str, str]) -> None:
    if re.search(
        r"payloadVersion\s*==\s*SESSION_OPENQ4_SAVEGAME_PREVIOUS_COMPATIBILITY_VERSION\s*&&\s*"
        r"payloadBuild\s*<\s*SESSION_OPENQ4_SAVEGAME_MINIMUM_SUPPORTED_BUILD",
        session,
    ) is None:
        raise AssertionError("Engine preflight applies the v3 build floor to payloads whose layout has no build gate")
    for context, source in games.items():
        body = re.search(
            r"bool idRestoreGame::HasOpenQ4PlayerLiquidSaveFieldForBuild\( int firstBuildWithField \) const \{(?P<body>.*?)\n\}",
            source,
            re.DOTALL,
        )
        if body is None:
            raise AssertionError(f"{context} has no per-field build gate")
        text = body.group("body")
        later = re.search(
            r"openQ4SaveGameCompatibilityVersion\s*>\s*OPENQ4_SAVEGAME_PREVIOUS_COMPATIBILITY_VERSION\s*\)\s*\{\s*return\s+true;",
            text,
        )
        build = text.find("buildNumber >= firstBuildWithField")
        if later is None or build < 0 or later.start() > build:
            raise AssertionError(f"{context} consults the writer's build number for payloads newer than v3")


def main() -> None:
    session = read(ROOT / "src" / "framework" / "Session.cpp")
    games = {
        "SP GameLib": read(ROOT / "src" / "game" / "gamesys" / "SaveGame.cpp"),
        "MP GameLib": read(ROOT / "src" / "mpgame" / "gamesys" / "SaveGame.cpp"),
    }
    current = constant(session, "SESSION_OPENQ4_SAVEGAME_COMPATIBILITY_VERSION")
    previous = constant(session, "SESSION_OPENQ4_SAVEGAME_PREVIOUS_COMPATIBILITY_VERSION")
    tables = {context: layout_table(source, context) for context, source in games.items()}

    guis = shipped_guis()
    validate_classification(guis)
    validate_layout_pins(guis, current, previous, tables)
    for context, source in games.items():
        if "SaveGame_FindGuiLayoutForPayload( name, openQ4SaveGameCompatibilityVersion )" not in source:
            raise AssertionError(f"{context} does not choose a GUI's saved layout by payload version")
        # Copying only the state dictionary loses window values scripts set, such
        # as a visible boss bar; the engine migration carries those as well.
        if "ui->ReadFromSaveGameWithLayout( file, layout, restoredWindowValues )" not in source:
            raise AssertionError(f"{context} does not restore older GUI layouts through the engine window migration")
    validate_release_provenance(previous)
    validate_build_number_scope(session, games)
    print("savegame_gui_layout_contract: ok")


if __name__ == "__main__":
    main()
