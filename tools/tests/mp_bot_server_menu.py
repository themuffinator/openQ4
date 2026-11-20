#!/usr/bin/env python3
"""Guards the bot rows in the create-server advanced options.

The multiplayer create-server menu can start a match with bots, which means the
menu has to agree with three things it cannot see:

  * `bot_minPlayers` and `bot_skill` are CVAR_GAME cvars declared by game_mp,
    but the menu is reachable while game_sp is loaded - the module is only
    swapped at spawnServer.  idChoiceWindow resolves its cvar once, when the gui
    is parsed, and does nothing at all when the name is absent, so the engine has
    to declare both before the main menu gui is loaded.
  * A choiceType 1 widget writes the raw string from its `values` list, so the
    list has to stay inside the range game_mp declares for the cvar, and it has
    to have exactly as many entries as the localized `choices` string, in every
    language.  A short choices list silently truncates the tail of the values.
  * The popup is a fixed three-slice frame with no scrolling.  Every row, the
    ban-list row and the close button have to stay inside the 480-unit virtual
    screen, and each option row needs its highlight bar and its invisible mouse
    target on the same line.
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)).resolve()

MAIN_MENU = ROOT / "content" / "baseoq4" / "pak0" / "guis" / "mainmenu.gui"
STRINGS = ROOT / "content" / "baseoq4" / "pak0" / "strings"

VIRTUAL_SCREEN_HEIGHT = 480

# The two rows this test exists for: widget name, cvar, and the string id whose
# translated value supplies the visible choices.
BOT_ROWS = (
    ("pop_createAdv_botFill_val", "bot_minPlayers", "#str_42211"),
    ("pop_createAdv_botSkill_val", "bot_skill", "#str_42213"),
)

# Row labels. A missing id is not a blank row - the gui draws the raw key.
BOT_ROW_LABELS = (
    ("pop_createAdv_botFill", "#str_42210"),
    ("pop_createAdv_botSkill", "#str_42212"),
)

# 42300 to 42800 is the closed Match Control localization bridge range, where
# every id has to be referenced by the match-control sources - see
# tools/tests/match_control_localization_bridge.py. Menu strings belong below it.
MATCH_CONTROL_BRIDGE_RANGE = (42300, 42800)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def gui_block(source_text: str, widget_name: str) -> str:
    pattern = re.compile(rf"\b(?:windowDef|choiceDef|editDef)\s+{re.escape(widget_name)}\b")
    match = pattern.search(source_text)
    if match is None:
        raise AssertionError(f"Missing GUI widget block for {widget_name}")

    brace_start = source_text.find("{", match.end())
    depth = 0
    for index in range(brace_start, len(source_text)):
        if source_text[index] == "{":
            depth += 1
        elif source_text[index] == "}":
            depth -= 1
            if depth == 0:
                return source_text[brace_start : index + 1]

    raise AssertionError(f"Unterminated GUI widget block for {widget_name}")


def gui_rect(source_text: str, widget_name: str) -> tuple[int, int, int, int]:
    block = gui_block(source_text, widget_name)
    match = re.search(r"^\s*rect\s+(-?\d+),(-?\d+),(-?\d+),(-?\d+)\s*$", block, re.MULTILINE)
    if match is None:
        raise AssertionError(f"{widget_name} has no literal rect")
    return tuple(int(value) for value in match.groups())  # type: ignore[return-value]


def gui_value(block: str, key: str, widget_name: str) -> str:
    match = re.search(rf'^\s*{key}\s+"?([^"\n]+?)"?\s*$', block, re.MULTILINE)
    if match is None:
        raise AssertionError(f"Missing {key!r} in {widget_name}")
    return match.group(1)


def localized_tables() -> list[tuple[str, Path]]:
    tables = [(path.stem.split("_", 1)[0], path) for path in sorted(STRINGS.glob("*_openq4.lang"))]
    if not tables:
        raise AssertionError("No openQ4 string tables found")
    return tables


def localized_choices(string_id: str) -> dict[str, list[str]]:
    """Return the semicolon-separated choice list per language."""
    per_language: dict[str, list[str]] = {}
    for language, path in localized_tables():
        match = re.search(rf'^\s*"{re.escape(string_id)}"\s+"([^"]*)"', read(path), re.MULTILINE)
        if match is None:
            raise AssertionError(f"Missing {string_id} in the {language} string table")
        per_language[language] = match.group(1).split(";")
    return per_language


def validate_engine_declares_menu_cvars() -> None:
    """The menu binds game_mp cvars while game_sp may be the loaded module."""
    source = read(ROOT / "src" / "framework" / "Session.cpp")

    require(source, "Session_DeclareMultiplayerMenuGameCVars", "Session.cpp")
    declare = source[source.index("static void Session_DeclareMultiplayerMenuGameCVars") :]
    declare = declare[: declare.index("\n}\n") + 3]

    for _, cvar_name, _ in BOT_ROWS:
        require(declare, f'"{cvar_name}"', "Session_DeclareMultiplayerMenuGameCVars")

    # Only declare what the module has not; an unconditional write would stomp
    # the value game_mp already registered.
    require(declare, "cvarSystem->Find(", "Session_DeclareMultiplayerMenuGameCVars")
    require(declare, "CVAR_GAME | CVAR_ARCHIVE | menuGameCVars[i].flags", "Session_DeclareMultiplayerMenuGameCVars")
    for _, cvar_name, _ in BOT_ROWS:
        if re.search(rf'\{{ "{re.escape(cvar_name)}",\s*"\d+",\s*CVAR_INTEGER \}}', declare) is None:
            raise AssertionError(f"Session_DeclareMultiplayerMenuGameCVars declares {cvar_name} without CVAR_INTEGER")

    # ...and it has to run before the gui is parsed, because idChoiceWindow
    # resolves its cvar exactly once, in InitVars.
    call = source.find("Session_DeclareMultiplayerMenuGameCVars();")
    parse = source.find('uiManager->FindGui( "guis/mainmenu.gui"')
    if call < 0:
        raise AssertionError("Session::Init no longer calls Session_DeclareMultiplayerMenuGameCVars")
    if parse < 0:
        raise AssertionError("Session::Init no longer loads guis/mainmenu.gui")
    if call > parse:
        raise AssertionError(
            "Session_DeclareMultiplayerMenuGameCVars runs after mainmenu.gui is parsed; "
            "the bot rows would bind nothing"
        )

    choice_window = read(ROOT / "src" / "ui" / "ChoiceWindow.cpp")
    require(choice_window, "cvar = cvarSystem->Find( cvarStr );", "idChoiceWindow::InitVars")


def validate_row_widgets() -> None:
    source = read(MAIN_MENU)

    for widget, string_id in BOT_ROW_LABELS:
        require(gui_block(source, widget), f'text\t"{string_id}"', widget)
        for language, path in localized_tables():
            if re.search(rf'^\s*"{re.escape(string_id)}"\s+"[^"\s][^"]*"', read(path), re.MULTILINE) is None:
                raise AssertionError(f"{widget}: {string_id} is missing or empty in the {language} string table")

    for widget, cvar_name, string_id in BOT_ROWS:
        block = gui_block(source, widget)

        low, high = MATCH_CONTROL_BRIDGE_RANGE
        if low < int(string_id.removeprefix("#str_")) < high:
            raise AssertionError(
                f"{widget} uses {string_id}, inside the closed Match Control bridge range"
            )

        require(block, f"cvar\t{cvar_name}", widget)
        if gui_value(block, "choiceType", widget) != "1":
            raise AssertionError(f"{widget} must use choiceType 1 so it writes the cvar value, not an index")

        values = gui_value(block, "values", widget).split(";")
        if gui_value(block, "choices", widget) != string_id:
            raise AssertionError(f"{widget} choices must come from {string_id}")

        for language, choices in localized_choices(string_id).items():
            if len(choices) != len(values):
                raise AssertionError(
                    f"{widget}: the {language} {string_id} has {len(choices)} choices "
                    f"for {len(values)} values; the tail would be unreachable"
                )

        if sorted(int(value) for value in values) != [int(value) for value in values]:
            raise AssertionError(f"{widget} values must ascend so the arrows read in one direction")

    fill_values = [int(value) for value in gui_value(gui_block(source, BOT_ROWS[0][0]), "values", BOT_ROWS[0][0]).split(";")]
    if fill_values[0] != 0:
        raise AssertionError("The bot fill row must offer 0, the value that disables the fill")
    if fill_values[1] < 2:
        raise AssertionError("A fill target below 2 can never add a bot and would read as a broken setting")

    skill_values = [int(value) for value in gui_value(gui_block(source, BOT_ROWS[1][0]), "values", BOT_ROWS[1][0]).split(";")]
    declared = read(GAME_LIBS_ROOT / "src" / "mpgame" / "gamesys" / "SysCvar.cpp")
    skill_range = re.search(r'idCVar bot_skill\([^;]*?,\s*(\d+)\s*,\s*(\d+)\s*\)', declared, re.DOTALL)
    if skill_range is None:
        raise AssertionError("bot_skill no longer declares a numeric range in mpgame SysCvar.cpp")
    low, high = int(skill_range.group(1)), int(skill_range.group(2))
    if skill_values[0] != low or skill_values[-1] != high:
        raise AssertionError(
            f"The bot skill row offers {skill_values[0]}-{skill_values[-1]}, "
            f"but bot_skill clamps to {low}-{high}"
        )

    session = read(ROOT / "src" / "framework" / "Session.cpp")
    declare = session[session.index("static void Session_DeclareMultiplayerMenuGameCVars"):]
    declare = declare[: declare.index("\n}\n")]
    entries = re.findall(r'\{ "(\w+)",\s*"([^"]*)",\s*([A-Z_ |]+) \}', declare)
    if {name for name, _, _ in entries} < {cvar for _, cvar, _ in BOT_ROWS}:
        raise AssertionError("Session_DeclareMultiplayerMenuGameCVars no longer declares the bot rows' cvars")
    # A mismatched reset value makes idInternalCVar::Update warn about a cvar
    # given two initial values every time the module swaps in, so each menu
    # declaration carries game_mp's default (the bots' and the other
    # multiplayer settings the menus edit).
    sources = "".join(read(path) for path in sorted((GAME_LIBS_ROOT / "src" / "mpgame").rglob("*.cpp")))
    for name, value, kind in entries:
        found = re.search(rf'idCVar\s+{re.escape(name)}\(\s*"{re.escape(name)}",\s*"([^"]*)"', sources)
        if found is None:
            raise AssertionError(f"mpgame no longer declares {name}, which the menus declare for it")
        if found.group(1) != value:
            raise AssertionError(f"Session.cpp declares {name} as {value!r}; game_mp's default is {found.group(1)!r}")


def validate_popup_layout() -> None:
    source = read(MAIN_MENU)

    # Each option row is a highlight bar, a label, a value widget and an
    # invisible mouse target, all on the same line.
    rows = (
        ("pop_createAdv_b15", "pop_createAdv_botFill", "pop_createAdv_botFill_val", "pop_b_createAdv_15"),
        ("pop_createAdv_b16", "pop_createAdv_botSkill", "pop_createAdv_botSkill_val", "pop_b_createAdv_16"),
    )
    for bar, label, value, target in rows:
        bar_y = gui_rect(source, bar)[1]
        label_y = gui_rect(source, label)[1]
        value_y = gui_rect(source, value)[1]
        target_y = gui_rect(source, target)[1]
        if not (label_y == value_y == target_y):
            raise AssertionError(f"{label}: label, value and mouse target are not on one line")
        if label_y - bar_y != 2:
            raise AssertionError(f"{bar} does not sit behind {label}")

        for event in ("showPop_createAdv", "hidePop_createAdv"):
            handler = source[source.index(f"onNamedEvent {event}") :]
            handler = handler[: handler.index("\n\t\t}")]
            require(handler, f'"{target}::visible"', event)
            require(handler, f'"{value}::noevents"', event)

    # The popup does not scroll, so the last row has to stay on screen.
    top_y = gui_rect(source, "pop_createAdv_top")[1]
    mid_x, mid_y, mid_w, mid_h = gui_rect(source, "pop_createAdv_mid")
    btm_y, btm_h = gui_rect(source, "pop_createAdv_btm")[1], gui_rect(source, "pop_createAdv_btm")[3]
    close_y, close_h = gui_rect(source, "pop_createAdv_close")[1], gui_rect(source, "pop_createAdv_close")[3]

    if mid_y + mid_h != btm_y:
        raise AssertionError("The popup frame slices no longer meet; the middle panel would show a seam")
    if top_y < 0 or btm_y + btm_h > VIRTUAL_SCREEN_HEIGHT:
        raise AssertionError("The popup frame runs off the virtual screen")
    if close_y + close_h > VIRTUAL_SCREEN_HEIGHT:
        raise AssertionError("The close button runs off the bottom of the screen")

    # Bars are 25 tall on a 24 pitch - the art has a one-unit lip that is meant
    # to tuck under the next row, so rows are spaced, not merely non-overlapping.
    row_pitch = gui_rect(source, "pop_createAdv_b16")[1] - gui_rect(source, "pop_createAdv_b15")[1]
    banlist_y = gui_rect(source, "pop_createAdv_b10")[1]
    if row_pitch != 24:
        raise AssertionError(f"The bot rows are {row_pitch} apart, not the popup's 24-unit pitch")
    if banlist_y - gui_rect(source, "pop_createAdv_b16")[1] != row_pitch:
        raise AssertionError("The ban-list row does not follow the bot skill row on the same pitch")
    if banlist_y >= close_y:
        raise AssertionError("The ban-list row must stay above the close button")


def main() -> int:
    try:
        validate_engine_declares_menu_cvars()
        validate_row_widgets()
        validate_popup_layout()
    except AssertionError as error:
        print(f"mp_bot_server_menu: FAILED - {error}")
        return 1
    except FileNotFoundError as error:
        print(f"mp_bot_server_menu: FAILED - missing file {error.filename}")
        return 1

    print("mp_bot_server_menu: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
