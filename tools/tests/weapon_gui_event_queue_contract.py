#!/usr/bin/env python3
"""A weapon's queued view-GUI events must neither grow without end nor block a load.

rvWeapon posts named events (weapon_init, weapon_ammo, weapon_lower, ...) to
the shared view model, and rvWeapon::UpdateGUI hands them to the weapon's view
GUI. The blaster, gauntlet, grenade launcher, lightning gun and dark matter gun
have no view GUI, so retail kept every event they posted: one per shot, several
per weapon switch, written into each save. The 256-event restore cap then
rejected saves the same build had just written (issue #192: "invalid pending
GUI event count 372").

This contract checks:

* UpdateGUI drops queued events when the view model has no GUI to take them,
  while a holstered weapon with a GUI still keeps them for its raise;
* Restore bounds the count by the bytes left in the save, not a fixed cap, and
  still rejects a negative count;
* the write side is unchanged, so the save payload is too;
* SP and MP carry the same code.
"""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TREES = ("game", "mpgame")


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"Required source file not found: {path}")
    return path.read_text(encoding="utf-8")


def body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing {signature}")
    depth = 0
    for index in range(source.index("{", start), len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"Unbalanced body for {signature}")


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def accepted_count(num: int, remaining: int) -> bool:
    """The restore bound: each queued event is at least its 4-byte length."""
    return num >= 0 and (remaining < 0 or num <= remaining // 4)


def validate_bound_model() -> None:
    # The count from #192 and a larger one written by the same build both load
    # when the save holds their strings.
    for num in (0, 256, 372, 996):
        if not accepted_count(num, num * (4 + len("weapon_ammo")) + 1024):
            raise AssertionError(f"a save holding {num} queued events must restore")
    for num, remaining in ((-1, 1 << 20), (0x7FFFFFFF, 1 << 20), (1000, 3999)):
        if accepted_count(num, remaining):
            raise AssertionError(f"count {num} with {remaining} bytes left must be rejected")


def validate_tree(tree: str) -> dict[str, str]:
    weapon = read(ROOT / "src" / tree / "Weapon.cpp")
    save_cpp = read(ROOT / "src" / tree / "gamesys" / "SaveGame.cpp")
    save_h = read(ROOT / "src" / tree / "gamesys" / "SaveGame.h")
    context = f"{tree} rvWeapon"

    update = strip_comments(body(weapon, "void rvWeapon::UpdateGUI( void )"))
    no_gui = update.index("if ( !gui ) {")
    drop = update.index("viewModel->pendingGUIEvents.Clear();", no_gui)
    if update.index("return;", no_gui) < drop:
        raise AssertionError(f"{context}::UpdateGUI must drop queued events when there is no view GUI")
    holstered = update.index("if ( status == WP_HOLSTERED ) {")
    if "Clear()" in update[holstered:update.index("return;", holstered)]:
        raise AssertionError(f"{context}::UpdateGUI must keep events for a holstered weapon's raise")
    if not update.rstrip().endswith("viewModel->pendingGUIEvents.Clear();\n}"):
        raise AssertionError(f"{context}::UpdateGUI no longer delivers and clears the queue at its end")

    restore = strip_comments(body(weapon, "void rvViewWeapon::Restore( idRestoreGame *savefile )"))
    for token in (
        "const int remaining = savefile->RemainingBytes();",
        "num < 0 || ( remaining >= 0 && num > remaining / (int)sizeof( int ) )",
        'savefile->Error( "rvViewWeapon::Restore: invalid pending GUI event count %d", num );',
    ):
        if token not in restore:
            raise AssertionError(f"{context} restore lacks {token!r}")
    if "MAX_SAVEGAME_GUI_EVENTS" in weapon:
        raise AssertionError(f"{context} still caps queued events at a fixed count")

    save = strip_comments(body(weapon, "void rvViewWeapon::Save( idSaveGame *savefile ) const"))
    for token in ("savefile->WriteInt ( pendingGUIEvents.Num() );", "savefile->WriteString ( pendingGUIEvents[i] );"):
        if token not in save:
            raise AssertionError(f"{context} save layout changed: missing {token!r}")

    if "int\t\t\t\t\t\tRemainingBytes( void ) const;" not in save_h:
        raise AssertionError(f"{tree} idRestoreGame does not declare RemainingBytes")
    remaining = strip_comments(body(save_cpp, "int idRestoreGame::RemainingBytes( void ) const"))
    for token in ("file->Length()", "file->Tell()", "return -1;", "return length - offset;"):
        if token not in remaining:
            raise AssertionError(f"{tree} idRestoreGame::RemainingBytes lacks {token!r}")

    return {"UpdateGUI": update, "Restore": restore, "Save": save, "RemainingBytes": remaining}


def main() -> None:
    validate_bound_model()
    sp = validate_tree("game")
    mp = validate_tree("mpgame")
    for name in sp:
        if sp[name] != mp[name]:
            raise AssertionError(f"SP and MP {name} differ")
    print("weapon_gui_event_queue_contract: ok")


if __name__ == "__main__":
    main()
