#!/usr/bin/env python3
"""Static contract for the localized competitive Match Control surface."""

from __future__ import annotations

from collections import Counter
import os
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT.parent / "openQ4-game")
).resolve()
GUI_ROOT = ROOT / "content" / "baseoq4" / "pak0" / "guis"
STRINGS_ROOT = ROOT / "content" / "baseoq4" / "pak0" / "strings"
MP_MENU = GUI_ROOT / "mpmain.gui"
MATCH_GUI = GUI_ROOT / "matchcontrol.gui"
CONTRACT_DOC = ROOT / "docs" / "dev" / "match-control-ui-contract.md"
PROTOCOL_HEADER = GAME_ROOT / "src" / "mpgame" / "mp" / "match" / "MatchProtocol.h"
PROTOCOL_SOURCE = GAME_ROOT / "src" / "mpgame" / "mp" / "match" / "MatchProtocol.cpp"
OPERATIONS_SOURCE = GAME_ROOT / "src" / "mpgame" / "mp" / "match" / "MatchOperations.cpp"
SERIES_SOURCE = GAME_ROOT / "src" / "mpgame" / "mp" / "match" / "MatchSeries.cpp"
MULTIPLAYER_SOURCE = GAME_ROOT / "src" / "mpgame" / "MultiplayerGame.cpp"
LANGUAGES = (
    "english_openq4.lang",
    "french_openq4.lang",
    "italian_openq4.lang",
    "spanish_openq4.lang",
)
REQUIRED_DYNAMIC_LOCALIZATION_IDS = {
    "#str_41795",
    "#str_41796",
    "#str_42651",
    "#str_42652",
    "#str_42653",
    "#str_42344",
    "#str_42345",
    "#str_42349",
}

PAGES = ("status", "teams", "proposals", "rules", "series", "evidence")
LISTS = (
    "match_team_rows",
    "match_replacement_rows",
    "match_proposal_rows",
    "match_profile_rows",
    "match_rule_rows",
    "match_series_map_rows",
    "match_series_history_rows",
    "match_evidence_rows",
)
OPERATION_PREFIXES = (
    "ready_set",
    "team_ready_set",
    "force_ready",
    "team_join",
    "team_lock_set",
    "queue_join",
    "queue_defer",
    "queue_leave",
    "roster_leave",
    "timeout_request",
    "tech_pause_request",
    "resume_request",
    "ref_authenticate",
    "ref_logout",
    "rules_select_profile",
    "rules_stage_field",
    "rules_commit",
    "rules_discard",
    "proposal_create",
    "proposal_cast",
    "proposal_cancel",
    "roster_invite",
    "roster_accept",
    "roster_remove",
    "roster_substitute",
    "role_assign",
    "broadcaster_set",
    "series_stage_profile",
    "series_start",
    "series_cancel",
    "series_advance",
    "veto_select",
    "forfeit",
    "abort",
    "participant_remove",
    "series_contestant_bind",
)
SELECTION_COMMANDS = {
    "refresh",
    "select_team_row",
    "select_replacement_row",
    "select_proposal_row",
    "select_profile_row",
    "select_rule_row",
    "select_series_map",
    "action_side_a",
    "action_side_b",
}
COMMAND_TO_CANONICAL = {
    "ready_toggle": "ready_set",
    "team_ready_toggle": "team_ready_set",
    "force_ready": "force_ready",
    "team_join_marine": "team_join",
    "team_join_strogg": "team_join",
    "team_spectate": "team_join",
    "team_lock_toggle": "team_lock_set",
    "queue_join": "queue_join",
    "queue_defer": "queue_defer",
    "queue_leave": "queue_leave",
    "roster_leave": "roster_leave",
    "timeout": "timeout_request",
    "tech_pause": "tech_pause_request",
    "resume": "resume_request",
    "referee_login": "ref_authenticate",
    "referee_logout": "ref_logout",
    "rules_select_profile": "rules_select_profile",
    "rules_stage_field": "rules_stage_field",
    "rules_commit": "rules_commit",
    "rules_discard": "rules_discard",
    "proposal_create": "proposal_create",
    "proposal_yes": "proposal_cast",
    "proposal_no": "proposal_cast",
    "proposal_abstain": "proposal_cast",
    "proposal_cancel": "proposal_cancel",
    "roster_invite": "roster_invite",
    "roster_accept": "roster_accept",
    "roster_remove": "roster_remove",
    "roster_substitute": "roster_substitute",
    "role_assign": "role_assign",
    "broadcaster_set": "broadcaster_set",
    "series_stage": "series_stage_profile",
    "series_start": "series_start",
    "series_cancel": "series_cancel",
    "series_advance": "series_advance",
    "veto_ban": "veto_select",
    "veto_pick": "veto_select",
    "veto_decider": "veto_select",
    "veto_side_marine": "veto_select",
    "veto_side_strogg": "veto_select",
    "forfeit": "forfeit",
    "abort": "abort",
    "participant_remove": "participant_remove",
    "series_contestant_bind": "series_contestant_bind",
}
CONFIRMED_COMMANDS = (
    "force_ready",
    "rules_commit",
    "roster_remove",
    "roster_substitute",
    "series_start",
    "series_cancel",
    "series_advance",
    "veto_ban",
    "veto_pick",
    "veto_decider",
    "veto_side_marine",
    "veto_side_strogg",
    "forfeit",
    "abort",
    "participant_remove",
)
ARMED_COMMANDS = {f"arm_{command}" for command in CONFIRMED_COMMANDS}
COMMANDS = (
    SELECTION_COMMANDS
    | (set(COMMAND_TO_CANONICAL) - set(CONFIRMED_COMMANDS))
    | ARMED_COMMANDS
    | {"confirm", "cancel_confirm"}
    | {"follow_prev", "follow_next", "follow_free"}
)
DESCRIPTOR_RE = re.compile(
    r'\{\s*(?P<opcode>MP_MATCH_OP_[A-Z0-9_]+),\s*'
    r'"(?P<token>[a-z0-9_]+)",\s*'
    r'MP_MATCH_LOCALIZATION_OPERATION_[A-Z0-9_]+,\s*'
    r'(?P<confirmation>MP_MATCH_LOCALIZATION_(?:NONE|CONFIRM_[A-Z0-9_]+)),',
    re.DOTALL,
)
ENTRY_RE = re.compile(r'^\s*"(?P<id>#str_\d+)"\s+"(?P<value>.*)"\s*$')


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"Required file not found: {path}")
    return path.read_text(encoding="utf-8", errors="strict")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"Missing {token!r} in {context}")


def strip_strings_and_comments(text: str) -> str:
    text = re.sub(r"//[^\r\n]*", "", text)
    return re.sub(r'"(?:\\.|[^"\\])*"', '""', text)


def validate_balanced_gui(mp_menu: str, match_gui: str) -> None:
    expanded = mp_menu.replace('#include "guis/matchcontrol.gui"', match_gui)
    stripped = strip_strings_and_comments(expanded)
    depth = 0
    for offset, character in enumerate(stripped):
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth < 0:
                raise AssertionError(f"GUI closes a scope early at byte {offset}")
    if depth != 0:
        raise AssertionError(f"Expanded mpmain.gui has brace depth {depth}")


def parse_language(path: Path) -> dict[str, str]:
    entries: list[tuple[str, str]] = []
    for line in read(path).splitlines():
        match = ENTRY_RE.match(line)
        if match:
            entries.append((match.group("id"), match.group("value")))
    counts = Counter(identifier for identifier, _ in entries)
    duplicates = sorted(identifier for identifier, count in counts.items() if count > 1)
    if duplicates:
        raise AssertionError(f"{path.name} has duplicate IDs: {duplicates}")
    return dict(entries)


def extract_descriptor_array(text: str, name: str) -> str:
    match = re.search(
        rf"static const mpMatchArgumentDescriptor_t\s+{re.escape(name)}\[\]\s*=\s*"
        r"\{(?P<body>.*?)\n\};",
        text,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"Missing protocol argument descriptor {name}")
    return match.group("body")


def validate_protocol_contract(
    match_gui: str,
    protocol_header: str,
    protocol_source: str,
    operations_source: str,
    series_source: str,
) -> None:
    enum_entries = {
        name: int(value)
        for name, value in re.findall(
            r"^\s*(MP_MATCH_OP_[A-Z0-9_]+)\s*=\s*(\d+)",
            protocol_header,
            re.MULTILINE,
        )
        if name not in {"MP_MATCH_OP_INVALID", "MP_MATCH_OP_COUNT"}
    }
    descriptors = list(DESCRIPTOR_RE.finditer(protocol_source))
    descriptor_opcodes = [match.group("opcode") for match in descriptors]
    descriptor_tokens = [match.group("token") for match in descriptors]
    if len(descriptor_opcodes) != len(set(descriptor_opcodes)):
        raise AssertionError("Match protocol contains duplicate operation descriptors")
    if len(descriptor_tokens) != len(set(descriptor_tokens)):
        raise AssertionError("Match protocol contains duplicate canonical operation tokens")
    if set(descriptor_opcodes) != set(enum_entries):
        raise AssertionError(
            "Protocol descriptor/opcode drift; "
            f"missing={sorted(set(enum_entries) - set(descriptor_opcodes))}, "
            f"unexpected={sorted(set(descriptor_opcodes) - set(enum_entries))}"
        )
    if sorted(enum_entries.values()) != list(range(1, len(enum_entries) + 1)):
        raise AssertionError("Stable match-operation opcodes are no longer contiguous")
    if set(descriptor_tokens) != set(OPERATION_PREFIXES):
        raise AssertionError(
            "Match Control availability prefixes drifted from canonical protocol tokens; "
            f"missing={sorted(set(descriptor_tokens) - set(OPERATION_PREFIXES))}, "
            f"unexpected={sorted(set(OPERATION_PREFIXES) - set(descriptor_tokens))}"
        )
    if set(COMMAND_TO_CANONICAL.values()) != set(descriptor_tokens):
        raise AssertionError("Fixed Match Control commands do not cover every canonical operation")

    protocol_confirmations = {
        match.group("token")
        for match in descriptors
        if match.group("confirmation") != "MP_MATCH_LOCALIZATION_NONE"
    }
    expected_confirmations = {
        COMMAND_TO_CANONICAL[command] for command in CONFIRMED_COMMANDS
    }
    if protocol_confirmations != expected_confirmations:
        raise AssertionError(
            "Match Control confirmations drifted from protocol descriptor metadata; "
            f"missing={sorted(protocol_confirmations - expected_confirmations)}, "
            f"unexpected={sorted(expected_confirmations - protocol_confirmations)}"
        )

    modal_offset = match_gui.find("windowDef match_confirm_modal")
    if modal_offset < 0:
        raise AssertionError("Match Control confirmation modal is missing")
    modal = match_gui[modal_offset:]
    if match_gui.count('"matchControl confirm"') != 1 or \
            '"matchControl confirm"' not in modal:
        raise AssertionError("confirmation modal must emit one generic confirm token")
    if match_gui.count('"matchControl cancel_confirm"') != 1 or \
            '"matchControl cancel_confirm"' not in modal:
        raise AssertionError("confirmation modal must emit one cancellation token")
    for command in CONFIRMED_COMMANDS:
        direct = f'"matchControl {command}"'
        armed = f'"matchControl arm_{command}"'
        if direct in match_gui:
            raise AssertionError(
                f"{command} bypasses the prepared confirmation request"
            )
        if match_gui.count(armed) != 1 or armed in modal:
            raise AssertionError(
                f"{command} must arm exactly one typed request before opening the modal"
            )

    if match_gui.count('"matchControl queue_join"') != 1:
        raise AssertionError("Queue admission must expose one side-neutral queue_join command")
    for forbidden in ("queue_join_marine", "queue_join_strogg"):
        if forbidden in match_gui:
            raise AssertionError(f"Queue admission must not encode a side through {forbidden}")
    require(
        protocol_source,
        'MP_MATCH_OP_QUEUE_JOIN, "queue_join", MP_MATCH_LOCALIZATION_OPERATION_QUEUE_JOIN, MP_MATCH_LOCALIZATION_NONE,',
        "side-neutral queue descriptor",
    )

    expected_enum_values = {
        "MP_MATCH_TEAM_NONE": 0,
        "MP_MATCH_TEAM_MARINE": 1,
        "MP_MATCH_TEAM_STROGG": 2,
        "MP_MATCH_TEAM_SPECTATOR": 3,
        "MP_MATCH_BALLOT_YES": 1,
        "MP_MATCH_BALLOT_NO": 2,
        "MP_MATCH_BALLOT_ABSTAIN": 3,
        "MP_MATCH_PROTOCOL_ROSTER_ROLE_PLAYER": 1,
        "MP_MATCH_PROTOCOL_ROSTER_ROLE_CAPTAIN": 2,
        "MP_MATCH_PROTOCOL_ROSTER_ROLE_COACH": 3,
        "MP_MATCH_PROTOCOL_ROSTER_ROLE_SUBSTITUTE": 4,
        "MP_MATCH_VETO_BAN": 1,
        "MP_MATCH_VETO_PICK": 2,
        "MP_MATCH_VETO_DECIDER": 3,
        "MP_MATCH_VETO_SIDE": 4,
        "MP_MATCH_STARTING_SIDE_MARINE": 1,
        "MP_MATCH_STARTING_SIDE_STROGG": 2,
    }
    for name, value in expected_enum_values.items():
        if re.search(rf"\b{re.escape(name)}\s*=\s*{value}\b", protocol_header) is None:
            raise AssertionError(f"Protocol enum value drifted: {name} must remain {value}")

    substitute_args = extract_descriptor_array(protocol_source, "ARG_ROSTER_SUBSTITUTE")
    for token in (
        "MP_MATCH_ARG_REPLACEMENT_PARTICIPANT",
        "MP_MATCH_VALUE_PARTICIPANT_ID",
        "true",
    ):
        require(substitute_args, token, "roster substitution arguments")
    role_args = extract_descriptor_array(protocol_source, "ARG_ROLE")
    for token in (
        "MP_MATCH_PROTOCOL_ROSTER_ROLE_PLAYER",
        "MP_MATCH_PROTOCOL_ROSTER_ROLE_SUBSTITUTE",
    ):
        require(role_args, token, "role-assignment arguments")
    for token in (
        "case MP_MATCH_PROTOCOL_ROSTER_ROLE_SUBSTITUTE:",
        "rosterRole = MP_MATCH_ROSTER_SUBSTITUTE;",
        "case MP_MATCH_OP_ROSTER_SUBSTITUTE:",
        "MP_MATCH_ARG_REPLACEMENT_PARTICIPANT",
        "result.continuation.rosterRole = rosterSeat->role;",
    ):
        require(operations_source, token, "roster protocol implementation")

    broadcaster_descriptor = re.search(
        r"\{\s*MP_MATCH_OP_BROADCASTER_SET,\s*\"broadcaster_set\".*?"
        r"MP_MATCH_PROTOCOL_CAP_BROADCASTER_ASSIGN.*?"
        r"MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET\s*\|\s*"
        r"MP_MATCH_OPERATION_FLAG_REQUIRE_PARTICIPANT_TARGET.*?"
        r"ARG_ENABLED,\s*1\s*\}",
        protocol_source,
        re.DOTALL,
    )
    if broadcaster_descriptor is None:
        raise AssertionError(
            "Broadcaster assignment must require one participant target and one enabled bool"
        )
    enabled_args = extract_descriptor_array(protocol_source, "ARG_ENABLED")
    for token in ("MP_MATCH_ARG_ENABLED", "MP_MATCH_VALUE_BOOL", "true"):
        require(enabled_args, token, "broadcaster enabled argument")
    for token in (
        "case MP_MATCH_OP_BROADCASTER_SET:",
        "MPOperationBroadcasterTargetIsValid",
        "MP_MATCH_ROLE_BROADCASTER",
    ):
        require(operations_source, token, "broadcaster assignment implementation")

    participant_remove_descriptor = re.search(
        r'\{\s*MP_MATCH_OP_PARTICIPANT_REMOVE,\s*"participant_remove".*?'
        r'MP_MATCH_OPERATION_FLAG_PROPOSABLE\s*\|\s*'
        r'MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET\s*\|\s*'
        r'MP_MATCH_OPERATION_FLAG_REQUIRE_PARTICIPANT_TARGET.*?\}',
        protocol_source,
        re.DOTALL,
    )
    if participant_remove_descriptor is None:
        raise AssertionError(
            "Participant removal must retain one stable ParticipantId target"
        )
    contestant_bind_descriptor = re.search(
        r'\{\s*MP_MATCH_OP_SERIES_CONTESTANT_BIND,\s*"series_contestant_bind".*?'
        r'MP_MATCH_OPERATION_FLAG_ALLOW_PARTICIPANT_TARGET\s*\|\s*'
        r'MP_MATCH_OPERATION_FLAG_REQUIRE_PARTICIPANT_TARGET.*?'
        r'ARG_COMPETITION_SIDE,\s*1\s*\}',
        protocol_source,
        re.DOTALL,
    )
    if contestant_bind_descriptor is None:
        raise AssertionError(
            "Duel contestant binding must retain ParticipantId plus explicit A/B side"
        )

    proposal_cast = extract_descriptor_array(protocol_source, "ARG_PROPOSAL_CAST")
    for token in ("MP_MATCH_ARG_PROPOSAL_ID", "MP_MATCH_ARG_BALLOT_CHOICE"):
        require(proposal_cast, token, "proposal-cast arguments")
    proposal_cancel = extract_descriptor_array(protocol_source, "ARG_PROPOSAL_ID")
    require(proposal_cancel, "MP_MATCH_ARG_PROPOSAL_ID", "proposal-cancel arguments")
    if "MP_MATCH_ARG_PROPOSAL_SCOPE" in protocol_header + protocol_source:
        raise AssertionError("Proposal scope must be resolved from proposal ID, not serialized")
    if operations_source.count("FindProposalById( proposals, proposalId, scope )") < 2:
        raise AssertionError("Proposal cast and cancellation must resolve scope server-side")

    for profile_key in ('"best_of_one"', '"best_of_three"', '"best_of_five"'):
        require(series_source, profile_key, "stable series profile keys")


def main() -> None:
    mp_menu = read(MP_MENU)
    match_gui = read(MATCH_GUI)
    contract = read(CONTRACT_DOC)
    protocol_header = read(PROTOCOL_HEADER)
    protocol_source = read(PROTOCOL_SOURCE)
    operations_source = read(OPERATIONS_SOURCE)
    series_source = read(SERIES_SOURCE)
    multiplayer_source = read(MULTIPLAYER_SOURCE)

    require(mp_menu, '#include "guis/matchcontrol.gui"', "multiplayer menu")
    for token in (
        'float\t"match_tab"\t0',
        'float\t"match_confirm"\t0',
        'windowDef main_b_matchcontrol',
        '"desktop::dest" "21"',
        'windowDef anim_matchcontrolIn',
        'windowDef anim_matchcontrolOut',
        'set "gui::match_referee_credential" ""',
    ):
        require(mp_menu, token, "multiplayer menu integration")
    if mp_menu.count('set "gui::match_referee_credential" ""') < 3:
        raise AssertionError(
            "Referee credential must be cleared on activation, ESC, and panel exit"
        )
    validate_balanced_gui(mp_menu, match_gui)

    fragment_without_comments = re.sub(r"//[^\r\n]*", "", match_gui)
    definitions = re.findall(
        r"\b(?:windowDef|listDef|choiceDef|editDef)\s+([A-Za-z_]\w*)",
        fragment_without_comments,
    )
    duplicate_definitions = sorted(
        name for name, count in Counter(definitions).items() if count > 1
    )
    if duplicate_definitions:
        raise AssertionError(
            f"Match Control has duplicate window names: {duplicate_definitions}"
        )

    require(match_gui, "windowDef p_matchcontrol", "Match Control fragment")
    require(match_gui, 'visible\t( "gui::match_follow_visible" == 1 )', "spectator camera row")
    for direction, x in (("prev", 202), ("next", 322), ("free", 442)):
        require(match_gui, f"windowDef match_follow_{direction}_button", "camera button")
        require(match_gui, f'rect\t{x},344,116,24', "camera button layout")
        require(match_gui, f'"matchControl follow_{direction}"', "fixed camera command")
    for language in (*LANGUAGES, "polish_openq4.lang", "russian_openq4.lang"):
        table = parse_language(STRINGS_ROOT / language)
        for identifier in range(42885, 42889):
            if not table.get(f"#str_{identifier}", "").strip():
                raise AssertionError(f"{language} lacks spectator camera localization {identifier}")
    require(match_gui, 'password\t1', "credential input")
    require(match_gui, 'maxchars\t64', "bounded credential input")
    require(match_gui, 'values\t"1;2;3;4"', "bounded roster roles")
    for token in (
        'visible\t( "gui::match_broadcaster_control_visible" == 1 )',
        'text\t"gui::match_broadcaster_action"',
        '"gui::match_op_broadcaster_set_available"',
        '"gui::match_op_broadcaster_set_reason"',
        '"matchControl broadcaster_set"',
    ):
        require(match_gui, token, "operator broadcaster control")
    for token in (
        '"gui::match_action_side_visible"',
        '"gui::match_action_side_0_enabled"',
        '"gui::match_action_side_1_enabled"',
        '"gui::match_action_side_0_selected"',
        '"gui::match_action_side_1_selected"',
        '"matchControl action_side_a"',
        '"matchControl action_side_b"',
    ):
        require(match_gui, token, "explicit side target control")
    for token in (
        'visible\t( "gui::match_op_series_contestant_bind_available" == 1 )',
        '"matchControl series_contestant_bind"',
        '"matchControl arm_participant_remove"',
        'set "match_confirm_body::text" "#str_42349"',
    ):
        require(match_gui, token, "stable participant action controls")
    if re.search(
        r'\{\s*"arm_participant_remove"\s*,\s*'
        r'MP_MATCH_CONTROL_COMMAND_PARTICIPANT_REMOVE\s*\}',
        multiplayer_source,
    ) is None:
        raise AssertionError(
            "Live Match Control ingress does not recognize the fixed participant-removal arm token"
        )
    role_choice = re.search(
        r"choiceDef\s+match_role_choice\s*\{(?P<body>.*?)\n\s*\}",
        match_gui,
        re.DOTALL,
    )
    if role_choice is None or "choiceType\t1" not in role_choice.group("body"):
        raise AssertionError("Roster-role choice must retain protocol values 1..4")
    require(match_gui, 'values\t"global;side"', "bounded proposal scope")
    require(match_gui, "windowDef match_confirm_modal", "destructive-action confirmation")
    require(match_gui, "modal\t1", "destructive-action confirmation")
    require(
        match_gui,
        'values\t"best_of_one;best_of_three;best_of_five"',
        "bounded series profiles",
    )
    for page in PAGES:
        require(match_gui, f"windowDef match_{page}_page", "six-tab surface")
    for list_name in LISTS:
        require(match_gui, f"listname\t{list_name}", "bounded view lists")

    for prefix in OPERATION_PREFIXES:
        require(
            match_gui,
            f'"gui::match_op_{prefix}_available"',
            f"{prefix} availability",
        )
        require(
            match_gui,
            f'"gui::match_op_{prefix}_reason"',
            f"{prefix} localized denial",
        )

    combined = mp_menu + "\n" + match_gui
    actual_commands = set(re.findall(r'"matchControl\s+([a-z_]+)"', combined))
    if actual_commands != COMMANDS:
        raise AssertionError(
            "Match Control command registry drifted; "
            f"missing={sorted(COMMANDS - actual_commands)}, "
            f"unexpected={sorted(actual_commands - COMMANDS)}"
        )
    if re.search(r'"matchControl[^"\r\n]*(?:gui::|\$)', combined):
        raise AssertionError("Match Control command text must never interpolate GUI state")
    for forbidden in ("consolecmd", "exec ", "rcon ", "say "):
        if forbidden in match_gui.lower():
            raise AssertionError(f"Match Control fragment contains forbidden command path {forbidden!r}")

    validate_protocol_contract(
        match_gui,
        protocol_header,
        protocol_source,
        operations_source,
        series_source,
    )

    # Static visible copy is always localized. Dynamic text must arrive through
    # gui:: state after the adapter localizes it.
    for match in re.finditer(
        r'\btext\s+"(?P<value>[^"]*)"', fragment_without_comments
    ):
        value = match.group("value")
        if not (value.startswith("#str_") or value.startswith("gui::")):
            raise AssertionError(f"Unlocalized visible Match Control text: {value!r}")
    for match in re.finditer(
        r'\bchoices\s+"(?P<value>[^"]*)"', fragment_without_comments
    ):
        value = match.group("value")
        if not (value.startswith("#str_") or value.startswith("gui::")):
            raise AssertionError(f"Unlocalized Match Control choice: {value!r}")

    referenced_ids = set(re.findall(r"#str_(?:417|419)\d{2}", combined))
    referenced_ids.update(REQUIRED_DYNAMIC_LOCALIZATION_IDS)
    if not referenced_ids:
        raise AssertionError("Match Control does not reference its localization block")
    tables = {name: parse_language(STRINGS_ROOT / name) for name in LANGUAGES}
    for name, table in tables.items():
        missing = sorted(referenced_ids - set(table))
        if missing:
            raise AssertionError(f"{name} is missing Match Control IDs: {missing}")
        empty = sorted(identifier for identifier in referenced_ids if not table[identifier].strip())
        if empty:
            raise AssertionError(f"{name} has empty Match Control IDs: {empty}")

    for token in (
        "mpMatchViewOperationAvailability",
        "aggregate control revision",
        "parallel bounded C++ row model",
        "wipe every temporary buffer",
        "do not parse tab-separated display text",
        "side-neutral",
        "MP_MATCH_ARG_REPLACEMENT_PARTICIPANT",
        "MP_MATCH_ARG_ENABLED",
        "match_broadcaster_control_visible",
        "Proposal scope is never serialized",
        "descriptor-advertised confirmation",
    ):
        require(contract, token, "adapter contract")

    print("match_control_ui_contract: ok")


if __name__ == "__main__":
    main()
