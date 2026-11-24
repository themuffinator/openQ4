#!/usr/bin/env python3
"""Cross-repository contracts for the public competitive multiplayer surface."""

from __future__ import annotations

import os
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)
).resolve()

PUBLIC_VALUES = (
    "DM;Tourney;Team DM;CTF;Arena CTF;DeadZone;Duel;Clan Arena;"
    "Freeze Tag;Red Rover;One Flag CTF;Arena One Flag CTF"
)
PUBLIC_TOKENS = [
    "DM",
    "Tourney",
    "Team DM",
    "CTF",
    "Arena CTF",
    "DeadZone",
    "Duel",
    "Clan Arena",
    "Freeze Tag",
    "Red Rover",
    "One Flag CTF",
    "Arena One Flag CTF",
]
HIDDEN_TOKENS = ["Overload", "Harvester", "Domination", "Attack Defend"]
MATCH_CONTEXT_TEXT_STATES = (
    "match_context_phase",
    "match_context_role",
    "match_context_pause",
    "match_context_readiness",
    "match_context_timeouts",
    "match_context_proposal",
    "match_context_series",
    "match_context_items",
)


def read(root: Path, relative_path: str) -> str:
    path = root / relative_path
    if not path.is_file():
        raise AssertionError(f"Required file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"Missing {token!r} in {context}")


def extract_window(text: str, name: str) -> str:
    """Return one balanced GUI windowDef, ignoring braces in strings/comments."""

    definitions = list(re.finditer(rf"\bwindowDef\s+{re.escape(name)}\b", text))
    if len(definitions) != 1:
        raise AssertionError(
            f"Expected exactly one windowDef {name!r}, found {len(definitions)}"
        )

    opening = text.find("{", definitions[0].end())
    if opening < 0:
        raise AssertionError(f"windowDef {name!r} has no body")

    depth = 0
    in_string = False
    in_comment = False
    escaped = False
    index = opening
    while index < len(text):
        character = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""

        if in_comment:
            if character in "\r\n":
                in_comment = False
        elif in_string:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                in_string = False
        elif character == "/" and following == "/":
            in_comment = True
            index += 1
        elif character == '"':
            in_string = True
        elif character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return text[definitions[0].start() : index + 1]
        index += 1

    raise AssertionError(f"windowDef {name!r} is not balanced")


def function_body(text: str, signature: str) -> str:
    """Return the balanced body of the C++ function defined with `signature`."""

    start = text.find(signature)
    if start < 0 or text.find(signature, start + 1) >= 0:
        raise AssertionError(f"Expected exactly one definition of {signature!r}")
    opening = text.find("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening : index + 1]
    raise AssertionError(f"{signature!r} is not balanced")


def validate_team_join_balance(multiplayer: str) -> None:
    """Match Control's committed team and the player's legacy team agree.

    A voluntary join is checked against si_autobalance before anything
    commits, and the legacy player then takes the committed side instead of
    balancing it again in idPlayer::UserInfoChanged.
    """

    continuation = function_body(
        multiplayer, "bool idMultiplayerGame::ApplyMatchOperationContinuation(")
    start = continuation.find("kind == MP_OPERATION_CONTINUATION_TEAM_CHANGE")
    end = continuation.find("kind == MP_OPERATION_CONTINUATION_TEAM_LOCK", start)
    if start < 0 or end < 0:
        raise AssertionError("Could not locate the team-change continuation")
    team_change = continuation[start:end]
    refusal = team_change.find("MatchTeamJoinUnbalances(")
    commit = team_change.find("ApplyMatchTeamsTransaction( decision, execution )")
    if refusal < 0 or commit < 0 or refusal > commit:
        raise AssertionError("A team join must face si_autobalance before it commits")
    require(team_change[refusal:commit], "MP_MATCH_PROTOCOL_REASON_TEAM_BALANCE",
            "team-balance refusal reason")
    # A roster seat fixes its holder's side; the balance never moves them.
    require(team_change[:refusal], "FindRosterSeat( execution.continuation.participant ) < 0",
            "rostered team-join exemption")

    balance = function_body(multiplayer, "bool idMultiplayerGame::MatchTeamJoinUnbalances(")
    require(balance, "VerifyTeamSwitch( side,", "team-join balance rule")

    publish = function_body(multiplayer, "void idMultiplayerGame::ApplyMatchTeamsPlanToLegacy(")
    marked = publish.find("matchTeamApplicationSlot = slot;")
    applied = publish.find("gameLocal.SetUserInfo( slot, updated, false );")
    restored = publish.find("matchTeamApplicationSlot = previousApplication;")
    if not 0 <= marked < applied < restored:
        raise AssertionError("The committed side must be published without re-balancing")

    verify = function_body(multiplayer, "int idMultiplayerGame::VerifyTeamSwitch(")
    exemption = verify.find("player->entityNumber == matchTeamApplicationSlot")
    counting = verify.find("teamCount[ TEAM_MARINE ] = teamCount[ TEAM_STROGG ] = 0;")
    if exemption < 0 or counting < 0 or exemption > counting:
        raise AssertionError("VerifyTeamSwitch must leave a committed match-layer side alone")


def validate_match_context_surface(
    gui: str,
    *,
    window_name: str,
    expected_rect: str,
    context: str,
) -> None:
    block = extract_window(gui, window_name)
    normalized_rect = r"\s*,\s*".join(re.escape(part) for part in expected_rect.split(","))
    if re.search(rf"\brect\s+{normalized_rect}\b", block) is None:
        raise AssertionError(f"{context} moved from required rectangle {expected_rect}")

    gate = re.findall(
        r'\bvisible\s*\(\s*"gui::match_context_visible"\s*==\s*1\s*\)',
        block,
    )
    if len(gate) != 1 or gui.count('"gui::match_context_visible"') != 1:
        raise AssertionError(f"{context} must use exactly one managed-match gate")

    text_sources = re.findall(r'^\s*text\s+"([^"]*)"', block, re.MULTILINE)
    expected_sources = [f"gui::{state}" for state in MATCH_CONTEXT_TEXT_STATES]
    if sorted(text_sources) != sorted(expected_sources):
        raise AssertionError(
            f"{context} text projection drifted: {text_sources!r} != {expected_sources!r}"
        )
    for source in expected_sources:
        if gui.count(f'"{source}"') != 1:
            raise AssertionError(f"{context} must project {source!r} exactly once")

    for interactive_token in ("onAction", "onEnter", "set ", '"cmd"', "listDef", "choiceDef"):
        if interactive_token in block:
            raise AssertionError(
                f"{context} is presentation-only but contains {interactive_token!r}"
            )
    if re.search(r"\bnoevents\s+1\b", block) is None:
        raise AssertionError(f"{context} must reject input with noevents 1")


def main() -> None:
    if not GAME_ROOT.is_dir():
        raise AssertionError(f"Companion game repository not found: {GAME_ROOT}")

    game_types = read(GAME_ROOT, "src/game/mp/GameTypes.cpp")
    multiplayer = read(GAME_ROOT, "src/game/MultiplayerGame.cpp")
    main_menu = read(ROOT, "content/baseoq4/pak0/guis/mainmenu.gui")
    mp_menu = read(ROOT, "content/baseoq4/pak0/guis/mpmain.gui")
    mp_hud = read(ROOT, "content/baseoq4/pak0/guis/mphud.gui")
    scoreboard = read(ROOT, "content/baseoq4/pak0/guis/scoreboard.gui")
    presentation_contract = read(ROOT, "docs/dev/managed-match-presentation-contract.md")
    server_scan = read(ROOT, "src/framework/async/ServerScan.cpp")
    async_client = read(ROOT, "src/framework/async/AsyncClient.cpp")
    session_menu = read(ROOT, "src/framework/Session_menu.cpp")

    # Hosting, both vote/admin choices and the game table expose the same
    # implemented subset. Reserved wire modes never appear on those surfaces.
    require(main_menu, f'values\t"{PUBLIC_VALUES}"', "host menu")
    if mp_menu.count(f'values\t"{PUBLIC_VALUES}"') != 2:
        raise AssertionError("Vote and admin menus must share the public mode list")

    completion = re.search(
        r"const char \*si_gameTypeArgs\[\] = \{(?P<body>.*?)\};",
        game_types,
        re.DOTALL,
    )
    vote_order = re.search(
        r"static const int mpVoteGameTypeOrder\[\] = \{(?P<body>.*?)\};",
        game_types,
        re.DOTALL,
    )
    browser = re.search(
        r"const char\* l_gameTypes\[\] = \{(?P<body>.*?)\};",
        server_scan,
        re.DOTALL,
    )
    if completion is None or vote_order is None or browser is None:
        raise AssertionError("Could not locate a public gametype registry")

    for token in HIDDEN_TOKENS:
        for body, context in (
            (completion.group("body"), "si_gameTypeArgs"),
            (main_menu, "host menu"),
            (mp_menu, "vote/admin menu"),
            (browser.group("body"), "browser filter"),
        ):
            if token in body:
                raise AssertionError(f"Hidden mode {token!r} leaked into {context}")

    browser_tokens = re.findall(r'"([^"]+)"', browser.group("body"))
    if browser_tokens != PUBLIC_TOKENS:
        raise AssertionError(
            f"Browser registry drifted: {browser_tokens!r} != {PUBLIC_TOKENS!r}"
        )

    for token in (
        "l_gameTypes[ i ] == NULL || i != requestedGameType",
        "Unknown/hidden tokens fail closed",
    ):
        require(server_scan, token, "fail-closed browser filter")
    # The master is asked for every server and the list applies the full
    # registry itself, so the legacy two-bit field never names a wrong mode.
    for token in (
        "The master is asked for every server",
        "msg.WriteBits( 0, 2 );",
        "extMsg.WriteBits( 0, 2 );",
    ):
        require(async_client, token, "legacy master compatibility")
    if 'GetCVarInteger( "gui_filter_gameType" )' in async_client:
        raise AssertionError("the master request still sends a game type filter")

    for token in (
        'gameType, "Duel"',
        'gameType, "Clan Arena"',
        'gameType, "Freeze Tag"',
        'gameType, "Red Rover"',
        'gameType, "One Flag CTF"',
        'gameType, "Arena One Flag CTF"',
    ):
        require(session_menu, token, "host map/team mirror")

    # Duel must use the DM rows and round modes the team rows; no supported
    # appended enum may fall through to the Tourney bracket presentation.
    require(
        scoreboard,
        '"gui::gametype" == 1 || "gui::gametype" == 9',
        "Duel scoreboard route",
    )
    for value in ("10", "11", "12"):
        require(scoreboard, f'"gui::gametype" == {value}', "round scoreboard route")
    require(multiplayer, "GAME_DM || gameLocal.gameType == GAME_DUEL", "Duel rows")
    require(multiplayer, "gameLocal.gameType == GAME_TDM || roundMode", "round rows")
    require(multiplayer, 'common->GetLocalizedString( "#str_41404" )', "round limit")

    # A casual no-time-limit server may retain the stock positive si_overtime
    # default. Timed overtime is valid only when regulation itself is timed;
    # otherwise the typed rule import must normalize to sudden death with no
    # timed period instead of rejecting the complete rules snapshot.
    for token in (
        'const int timeLimitMinutes = gameLocal.serverInfo.GetInt( "si_timeLimit" );',
        "const bool useTimedOvertime = timeLimitMinutes > 0 && overtimeSeconds > 0;",
        "useTimedOvertime ? MP_OVERTIME_TIMED_PERIODS : MP_OVERTIME_SUDDEN_DEATH",
        "useTimedOvertime ? overtimeSeconds : 0",
    ):
        require(multiplayer, token, "casual overtime normalization")

    # The managed-match HUD and scoreboard are projections of exactly the same
    # pre-localized state set. One parent gate makes their casual path inert.
    validate_match_context_surface(
        mp_hud,
        window_name="oq4_match_context_hud",
        expected_rect="400,8,232,106",
        context="managed-match HUD",
    )
    validate_match_context_surface(
        scoreboard,
        window_name="oq4_match_context_scoreboard",
        expected_rect="83,427,477,49",
        context="managed-match scoreboard",
    )
    for state in ("match_context_visible", *MATCH_CONTEXT_TEXT_STATES):
        require(presentation_contract, f"`{state}`", "managed-match presentation contract")
    for rectangle in ("`400,8,232,106`", "`83,427,477,49`"):
        require(presentation_contract, rectangle, "managed-match presentation placement")

    validate_team_join_balance(multiplayer)

    print("competitive match cross-repository contracts: PASS")


if __name__ == "__main__":
    main()
