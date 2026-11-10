#!/usr/bin/env python3
"""Static contract checks for the single-player Arena Campaign.

The Arena Campaign crosses boundaries that the compiler cannot check together:

* a small lexer-authored manifest describes the full five-tier ladder;
* four language tables supply every selector, tier, match, and result string;
* the main menu hands control to a dedicated campaign GUI;
* the framework launches a private game_mp server, populates named bots, and
  persists unlocks while preserving the player's ordinary server settings;
* game_mp reports the authoritative final score back to the framework.

This test parses the manifest independently, pins its stock-only map and roster
contract, and checks the static integration points without requiring retail
assets or launching the renderer.
"""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)
).resolve()

CAMPAIGN_PATH = (
    ROOT / "content" / "baseoq4" / "pak0" / "arena" / "openq4_campaign.cfg"
)
STRINGS_DIR = ROOT / "content" / "baseoq4" / "pak0" / "strings"
MAIN_MENU = ROOT / "content" / "baseoq4" / "pak0" / "guis" / "mainmenu.gui"
ARENA_MENU = ROOT / "content" / "baseoq4" / "pak0" / "guis" / "arena_menu.gui"

EXPECTED_MAPS_AND_MODES = (
    ("mp/q4dm1", "Duel"),
    ("mp/q4dm2", "DM"),
    ("mp/q4dm3", "Team DM"),
    ("mp/q4dm4", "Duel"),
    ("mp/q4dm5", "DM"),
    ("mp/q4dm6", "Red Rover"),
    ("mp/q4dm7", "Clan Arena"),
    ("mp/q4tourney1", "Duel"),
    ("mp/q4dm8", "DM"),
    ("mp/q4dm9", "Team DM"),
    ("mp/q4dm11", "Clan Arena"),
    ("mp/q4dm11v1", "Duel"),
    ("mp/q4xdm10", "DM"),
    ("mp/q4xdm13", "Red Rover"),
    ("mp/q4xdm11", "Team DM"),
    ("mp/q4xtourney2", "Duel"),
    ("mp/q4xdm14", "Team DM"),
    ("mp/q4xdm15", "Clan Arena"),
    ("mp/q4dm11", "DM"),
    ("mp/q4xtourney1", "Duel"),
)

# Latest official map declarations (pak019) advertise these base entity layouts.
# Derived openQ4 modes deliberately borrow one of these keys.
STOCK_MAP_KEYS = {
    "mp/q4dm1": {"DM", "Team DM"},
    "mp/q4dm2": {"DM", "Team DM"},
    "mp/q4dm3": {"DM", "Team DM", "Tourney"},
    "mp/q4dm4": {"DM", "Team DM"},
    "mp/q4dm5": {"DM", "Team DM"},
    "mp/q4dm6": {"DM", "Team DM", "Tourney"},
    "mp/q4dm7": {"DM", "Team DM", "Tourney"},
    "mp/q4tourney1": {"DM", "Team DM", "Tourney"},
    "mp/q4dm8": {"DM", "Team DM", "Tourney"},
    "mp/q4dm9": {"DM", "Team DM", "Tourney"},
    "mp/q4dm11": {"DM", "Team DM", "Tourney"},
    "mp/q4dm11v1": {"DM", "Team DM", "Tourney"},
    "mp/q4xdm10": {"DM", "Team DM", "DeadZone"},
    "mp/q4xdm13": {"DM", "Team DM", "DeadZone"},
    "mp/q4xdm11": {"DM", "Team DM", "DeadZone"},
    "mp/q4xtourney2": {"DM", "Team DM", "Tourney"},
    "mp/q4xtourney1": {"DM", "Team DM", "Tourney"},
    "mp/q4xdm14": {"DM", "Team DM", "DeadZone"},
    "mp/q4xdm15": {"DM", "Team DM", "DeadZone"},
}

MODE_MAP_KEY = {
    "DM": "DM",
    "Duel": "DM",
    "Team DM": "Team DM",
    "Red Rover": "Team DM",
    "Clan Arena": "Team DM",
}

EXPECTED_ROSTERS = (
    (("Bagby", 0),),
    (("Anderson", 0), ("Bagby", 1), ("Tetzlaff", 0)),
    (
        ("Anderson", 0),
        ("Bagby", 1),
        ("Tetzlaff", 1),
        ("Sorg", 1),
        ("Strauss", 1),
    ),
    (("Sorg", 1),),
    (("Sorg", 0), ("Strauss", 0), ("Hollenbeck", 0)),
    (
        ("Sorg", 0),
        ("Strauss", 0),
        ("Hollenbeck", 0),
        ("Marsh", 1),
        ("Anderson", -1),
    ),
    (
        ("Sorg", 0),
        ("Strauss", 0),
        ("Hollenbeck", 1),
        ("Marsh", 1),
        ("Tetzlaff", -1),
    ),
    (("Marsh", 1),),
    (("Bidwell", 0), ("Cortez", 0), ("Morris", 0), ("Rhodes", 0)),
    (
        ("Bidwell", 0),
        ("Cortez", 0),
        ("Morris", 0),
        ("Rhodes", 0),
        ("Voss", 1),
    ),
    (
        ("Bidwell", 0),
        ("Cortez", 0),
        ("Morris", 0),
        ("Rhodes", 0),
        ("Voss", 1),
    ),
    (("Voss", 1),),
    (("Gunner", 0), ("Rhodes", -1), ("Cortez", 0), ("Voss", 0)),
    (
        ("Gunner", 0),
        ("Rhodes", -1),
        ("Cortez", 0),
        ("Voss", 0),
        ("Kane", 1),
    ),
    (
        ("Gunner", 0),
        ("Kane", 0),
        ("Sledge", 0),
        ("Voss", 0),
        ("Rhodes", -1),
    ),
    (("Sledge", 1),),
    (
        ("Kane", 0),
        ("Sledge", 0),
        ("Gunner", 0),
        ("Morris", -1),
        ("Voss", 0),
    ),
    (
        ("Kane", 0),
        ("Sledge", 0),
        ("Gunner", 0),
        ("Morris", -1),
        ("Voss", 0),
    ),
    (
        ("Kane", 0),
        ("Sledge", 0),
        ("Gunner", 0),
        ("Morris", -1),
        ("Voss", 0),
    ),
    (("Makron", 0),),
)

REQUIRED_ROSTER = {
    "Anderson",
    "Bagby",
    "Bidwell",
    "Cortez",
    "Gunner",
    "Hollenbeck",
    "Kane",
    "Makron",
    "Marsh",
    "Morris",
    "Rhodes",
    "Sledge",
    "Sorg",
    "Strauss",
    "Tetzlaff",
    "Voss",
}


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"Missing {relative(path)}")
    return path.read_text(encoding="utf-8")


def relative(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_regex(haystack: str, pattern: str, context: str) -> None:
    if re.search(pattern, haystack, re.DOTALL) is None:
        raise AssertionError(f"Missing /{pattern}/ in {context}")


TOKEN_RE = re.compile(
    r"""
      (?P<comment>//[^\n]*|/\*.*?\*/)
    | (?P<string>"(?:[^"\\\n]|\\.)*")
    | (?P<int>-?\d+)
    | (?P<word>[A-Za-z_][A-Za-z0-9_]*)
    | (?P<brace>[{}])
    | (?P<space>\s+)
    """,
    re.VERBOSE | re.DOTALL,
)


@dataclass(frozen=True)
class Token:
    kind: str
    value: str
    line: int


def tokenize(source: str) -> list[Token]:
    tokens: list[Token] = []
    position = 0
    line = 1
    while position < len(source):
        match = TOKEN_RE.match(source, position)
        if match is None:
            excerpt = source[position : position + 30].splitlines()[0]
            raise AssertionError(
                f"{relative(CAMPAIGN_PATH)}:{line}: invalid token near {excerpt!r}"
            )
        text = match.group(0)
        kind = match.lastgroup or ""
        if kind not in {"space", "comment"}:
            value = text[1:-1] if kind == "string" else text
            tokens.append(Token(kind, value, line))
        line += text.count("\n")
        position = match.end()
    return tokens


class Parser:
    def __init__(self, tokens: list[Token]) -> None:
        self.tokens = tokens
        self.index = 0

    def peek(self) -> Token | None:
        return self.tokens[self.index] if self.index < len(self.tokens) else None

    def take(self, value: str | None = None, kind: str | None = None) -> Token:
        token = self.peek()
        if token is None:
            raise AssertionError(f"{relative(CAMPAIGN_PATH)}: unexpected end of file")
        if value is not None and token.value != value:
            raise AssertionError(
                f"{relative(CAMPAIGN_PATH)}:{token.line}: expected {value!r}, "
                f"found {token.value!r}"
            )
        if kind is not None and token.kind != kind:
            raise AssertionError(
                f"{relative(CAMPAIGN_PATH)}:{token.line}: expected {kind}, "
                f"found {token.kind} {token.value!r}"
            )
        self.index += 1
        return token


@dataclass(frozen=True)
class Bot:
    name: str
    offset: int


@dataclass(frozen=True)
class Match:
    name: str
    description: str
    map: str
    game_type: str
    base_skill: int
    frag_limit: int
    time_limit: int
    round_limit: int
    score_limit: int
    bots: tuple[Bot, ...]


@dataclass(frozen=True)
class Tier:
    name: str
    description: str
    matches: tuple[Match, ...]


@dataclass(frozen=True)
class Campaign:
    version: int
    tiers: tuple[Tier, ...]


def unique_field(seen: set[str], key: str, line: int) -> None:
    if key in seen:
        raise AssertionError(
            f"{relative(CAMPAIGN_PATH)}:{line}: duplicate field {key!r}"
        )
    seen.add(key)


def parse_bots(parser: Parser) -> tuple[Bot, ...]:
    parser.take("{")
    bots: list[Bot] = []
    while parser.peek() is not None and parser.peek().value != "}":
        parser.take("bot")
        name = parser.take(kind="string").value
        offset = int(parser.take(kind="int").value)
        bots.append(Bot(name, offset))
    parser.take("}")
    return tuple(bots)


def parse_match(parser: Parser) -> Match:
    parser.take("{")
    seen: set[str] = set()
    values: dict[str, object] = {}
    string_fields = {
        "name": "name",
        "description": "description",
        "map": "map",
        "gameType": "game_type",
    }
    int_fields = {
        "baseSkill": "base_skill",
        "fragLimit": "frag_limit",
        "timeLimit": "time_limit",
        "roundLimit": "round_limit",
        "scoreLimit": "score_limit",
    }
    while parser.peek() is not None and parser.peek().value != "}":
        field = parser.take(kind="word")
        unique_field(seen, field.value, field.line)
        if field.value in string_fields:
            values[string_fields[field.value]] = parser.take(kind="string").value
        elif field.value in int_fields:
            values[int_fields[field.value]] = int(parser.take(kind="int").value)
        elif field.value == "bots":
            values["bots"] = parse_bots(parser)
        else:
            raise AssertionError(
                f"{relative(CAMPAIGN_PATH)}:{field.line}: unknown match field "
                f"{field.value!r}"
            )
    parser.take("}")

    expected = set(string_fields) | set(int_fields) | {"bots"}
    if seen != expected:
        missing = sorted(expected - seen)
        extra = sorted(seen - expected)
        raise AssertionError(
            f"campaign match fields differ: missing={missing}, extra={extra}"
        )
    return Match(**values)  # type: ignore[arg-type]


def parse_tier(parser: Parser) -> Tier:
    parser.take("{")
    seen: set[str] = set()
    name = ""
    description = ""
    matches: list[Match] = []
    while parser.peek() is not None and parser.peek().value != "}":
        field = parser.take(kind="word")
        if field.value == "match":
            matches.append(parse_match(parser))
        elif field.value == "name":
            unique_field(seen, field.value, field.line)
            name = parser.take(kind="string").value
        elif field.value == "description":
            unique_field(seen, field.value, field.line)
            description = parser.take(kind="string").value
        else:
            raise AssertionError(
                f"{relative(CAMPAIGN_PATH)}:{field.line}: unknown tier field "
                f"{field.value!r}"
            )
    parser.take("}")
    if seen != {"name", "description"}:
        raise AssertionError("every campaign tier requires one name and description")
    return Tier(name, description, tuple(matches))


def parse_campaign() -> Campaign:
    parser = Parser(tokenize(read(CAMPAIGN_PATH)))
    parser.take("campaign")
    version = int(parser.take(kind="int").value)
    parser.take("{")
    tiers: list[Tier] = []
    while parser.peek() is not None and parser.peek().value != "}":
        parser.take("tier")
        tiers.append(parse_tier(parser))
    parser.take("}")
    if parser.peek() is not None:
        token = parser.peek()
        raise AssertionError(
            f"{relative(CAMPAIGN_PATH)}:{token.line}: data after campaign block"
        )
    return Campaign(version, tuple(tiers))


def validate_manifest(campaign: Campaign) -> None:
    if campaign.version != 1:
        raise AssertionError(f"campaign version is {campaign.version}, expected 1")
    if len(campaign.tiers) != 5:
        raise AssertionError(f"campaign has {len(campaign.tiers)} tiers, expected 5")

    matches: list[Match] = []
    for tier_index, tier in enumerate(campaign.tiers):
        expected_name = f"#str_{42050 + tier_index}"
        expected_description = f"#str_{42055 + tier_index}"
        if tier.name != expected_name or tier.description != expected_description:
            raise AssertionError(
                f"tier {tier_index + 1} uses {tier.name}/{tier.description}, "
                f"expected {expected_name}/{expected_description}"
            )
        if len(tier.matches) != 4:
            raise AssertionError(
                f"tier {tier_index + 1} has {len(tier.matches)} matches, expected 4"
            )

        for match_index, match in enumerate(tier.matches):
            global_index = tier_index * 4 + match_index
            expected_name = f"#str_{42100 + global_index * 2}"
            expected_description = f"#str_{42101 + global_index * 2}"
            if match.name != expected_name or match.description != expected_description:
                raise AssertionError(
                    f"match {global_index + 1} localization IDs drifted: "
                    f"{match.name}/{match.description}"
                )
            if match.base_skill != tier_index + 1:
                raise AssertionError(
                    f"tier {tier_index + 1} match {match_index + 1} baseSkill is "
                    f"{match.base_skill}, expected {tier_index + 1}"
                )
            if not 1 <= match.frag_limit <= 100:
                raise AssertionError(f"invalid fragLimit in match {global_index + 1}")
            if not 0 <= match.time_limit <= 60:
                raise AssertionError(f"invalid timeLimit in match {global_index + 1}")
            if not 1 <= match.round_limit <= 99:
                raise AssertionError(f"invalid roundLimit in match {global_index + 1}")
            if not 1 <= match.score_limit <= 9999:
                raise AssertionError(f"invalid scoreLimit in match {global_index + 1}")
            if not match.bots or len(match.bots) >= 16:
                raise AssertionError(f"invalid bot count in match {global_index + 1}")
            if len({bot.name.casefold() for bot in match.bots}) != len(match.bots):
                raise AssertionError(f"duplicate bot in match {global_index + 1}")
            for bot in match.bots:
                if bot.offset not in {-1, 0, 1}:
                    raise AssertionError(
                        f"{bot.name} has invalid offset {bot.offset} in "
                        f"match {global_index + 1}"
                    )
                if not 1 <= match.base_skill + bot.offset <= 5:
                    raise AssertionError(
                        f"{bot.name} falls outside base skill 1..5 in "
                        f"match {global_index + 1}"
                    )
            matches.append(match)

    actual_maps_and_modes = tuple((match.map, match.game_type) for match in matches)
    if actual_maps_and_modes != EXPECTED_MAPS_AND_MODES:
        raise AssertionError(
            "campaign stock-map progression drifted:\n"
            f"  actual:   {actual_maps_and_modes}\n"
            f"  expected: {EXPECTED_MAPS_AND_MODES}"
        )

    actual_rosters = tuple(
        tuple((bot.name, bot.offset) for bot in match.bots) for match in matches
    )
    if actual_rosters != EXPECTED_ROSTERS:
        raise AssertionError("campaign bot roster or skill offsets drifted")

    used_roster = {bot.name for match in matches for bot in match.bots}
    if used_roster != REQUIRED_ROSTER:
        raise AssertionError(
            f"campaign roster differs: missing={sorted(REQUIRED_ROSTER - used_roster)}, "
            f"extra={sorted(used_roster - REQUIRED_ROSTER)}"
        )

    character_dir = (
        ROOT / "content" / "baseoq4" / "pak0" / "botfiles" / "characters"
    )
    available_characters = {path.stem.casefold() for path in character_dir.glob("*.bot")}
    for bot_name in sorted(used_roster):
        if bot_name.casefold() not in available_characters:
            raise AssertionError(f"campaign bot {bot_name!r} has no character file")

    for index, match in enumerate(matches):
        if match.game_type not in MODE_MAP_KEY:
            raise AssertionError(
                f"match {index + 1} uses objective/unknown mode {match.game_type!r}"
            )
        if match.map.startswith("mp/q4cmp"):
            raise AssertionError("core campaign must not require the optional q4cmp pack")
        supported_keys = STOCK_MAP_KEYS.get(match.map)
        if supported_keys is None:
            raise AssertionError(f"match {index + 1} uses a non-contract stock map")
        needed_key = MODE_MAP_KEY[match.game_type]
        if needed_key not in supported_keys:
            raise AssertionError(
                f"{match.map} does not advertise {needed_key} for {match.game_type}"
            )
        if match.game_type in {"Team DM", "Red Rover", "Clan Arena"}:
            if len(match.bots) != 5:
                raise AssertionError(
                    f"team match {index + 1} needs five bots plus the local player"
                )

    for tier_index in range(5):
        boss = matches[tier_index * 4 + 3]
        if boss.game_type != "Duel" or len(boss.bots) != 1:
            raise AssertionError(
                f"tier {tier_index + 1} boss must be a one-bot Duel"
            )
    if matches[-1].bots[0].name != "Makron":
        raise AssertionError("final campaign boss must be Makron")

    # Preserve the ladder's authored breadth and escalation even if the pinned
    # map table is deliberately revised later.
    mode_counts = {
        mode: sum(match.game_type == mode for match in matches)
        for mode in MODE_MAP_KEY
    }
    if set(mode for mode, count in mode_counts.items() if count) != set(MODE_MAP_KEY):
        raise AssertionError(f"campaign mode variety regressed: {mode_counts}")
    for mode in ("DM", "Team DM", "Red Rover", "Clan Arena"):
        if mode_counts[mode] < 2:
            raise AssertionError(f"campaign needs at least two {mode} matches")

    unique_maps = {match.map for match in matches}
    if len(unique_maps) < len(matches) - 1:
        raise AssertionError(
            f"campaign repeats too many maps ({len(unique_maps)} unique for {len(matches)} matches)"
        )

    for match_index in range(4):
        slot = [campaign.tiers[tier].matches[match_index] for tier in range(5)]
        frag_limits = [match.frag_limit for match in slot]
        time_limits = [match.time_limit for match in slot]
        if frag_limits != sorted(frag_limits) or time_limits != sorted(time_limits):
            raise AssertionError(
                f"match slot {match_index + 1} no longer escalates its limits"
            )

    boss_names = [campaign.tiers[tier].matches[3].bots[0].name for tier in range(5)]
    if len(set(boss_names)) != len(boss_names):
        raise AssertionError(f"campaign boss roster repeats a character: {boss_names}")


LANG_ENTRY_RE = re.compile(r'^\s*"(#str_\d+)"\s+"((?:[^"\\]|\\.)*)"', re.MULTILINE)


def parse_language(path: Path) -> dict[str, str]:
    data = path.read_bytes()
    if data.startswith(b"\xef\xbb\xbf"):
        raise AssertionError(f"{relative(path)} must remain UTF-8 without a BOM")
    text = data.decode("utf-8")
    try:
        text.encode("cp1252")
    except UnicodeEncodeError as exc:
        raise AssertionError(
            f"{relative(path)} contains a character outside the CP1252 font set"
        ) from exc

    entries: dict[str, str] = {}
    for match in LANG_ENTRY_RE.finditer(text):
        key, value = match.groups()
        if key in entries:
            raise AssertionError(f"{relative(path)} contains duplicate {key}")
        entries[key] = value
    return entries


def validate_localization(campaign: Campaign) -> None:
    language_paths = {
        locale: STRINGS_DIR / f"{locale}_openq4.lang"
        for locale in ("english", "french", "italian", "spanish")
    }
    languages = {
        locale: parse_language(path) for locale, path in language_paths.items()
    }

    required_ids = (
        {f"#str_{value}" for value in range(42000, 42096)}
        | {f"#str_{value}" for value in range(42100, 42140)}
        # Arena launch and handoff failure reporting.
        | {f"#str_{value}" for value in range(42150, 42154)}
        # Per-match best result.
        | {"#str_42165", "#str_42166"}
        | {f"#str_{value}" for value in range(42200, 42205)}
    )
    referenced_ids = {
        tier.name for tier in campaign.tiers
    } | {
        tier.description for tier in campaign.tiers
    } | {
        value
        for tier in campaign.tiers
        for match in tier.matches
        for value in (match.name, match.description)
    } | set(re.findall(r"#str_\d+", read(ARENA_MENU)))

    for locale, entries in languages.items():
        missing = sorted(required_ids - entries.keys())
        if missing:
            raise AssertionError(f"{locale} arena strings missing: {missing}")
        empty = sorted(key for key in required_ids if not entries[key].strip())
        if empty:
            raise AssertionError(f"{locale} arena strings are empty: {empty}")
        if not referenced_ids <= entries.keys():
            raise AssertionError(f"{locale} lacks campaign-authored string references")

    english_expected = {
        "#str_42000": "SINGLE PLAYER",
        "#str_42001": "MISSION",
        "#str_42002": "ARENA",
        "#str_42009": "DIFFICULTY",
        "#str_42010": "START MATCH",
        "#str_42011": "RESET PROGRESS",
        "#str_42012": "CAMPAIGN PROGRESS",
        "#str_42022": "LOCKED",
        "#str_42023": "COMPLETE",
        "#str_42030": "VICTORY",
        "#str_42031": "DEFEAT",
        "#str_42033": "RETRY",
        "#str_42040": "BEST RESULT",
        "#str_42041": "Frags",
        "#str_42042": "Minutes",
        "#str_42043": "Rounds",
        "#str_42044": "Points",
        "#str_42045": "BOSS READY",
        "#str_42046": "NEW TIER UNLOCKED",
        "#str_42047": "FINAL TIER CONQUERED",
        "#str_42048": "MISSING REQUIRED MAP",
        "#str_42049": "Win all three qualifiers to reveal the boss, then win the boss match to unlock the next tier.",
        "#str_42060": "TRAINING",
        "#str_42061": "ROOKIE",
        "#str_42062": "VETERAN",
        "#str_42063": "NIGHTMARE",
        "#str_42064": "IMPOSSIBLE",
        "#str_42065": "AVAILABLE",
        "#str_42066": "Arena victory. The circuit opens ahead.",
        "#str_42067": "Arena defeated. Adjust your skill or replay the match.",
        "#str_42068": "MATCH ABORTED",
        "#str_42069": "One or more bots could not join. Check the console log and try again.",
        "#str_42070": "TIER BRIEFING",
        "#str_42071": "3 QUALIFIERS  >  BOSS  >  NEXT TIER",
        "#str_42072": "RESET ARENA PROGRESS?",
        "#str_42074": "CANCEL",
        "#str_42075": "RESET",
        "#str_42076": "REPLAY",
        "#str_42077": "DRAW",
        "#str_42078": "The match ended level. Replay it to settle the score.",
        "#str_42088": "QUALIFIER 1",
        "#str_42089": "QUALIFIER 2",
        "#str_42090": "QUALIFIER 3",
        "#str_42091": "TIER 1",
        "#str_42092": "TIER 2",
        "#str_42093": "TIER 3",
        "#str_42094": "TIER 4",
        "#str_42095": "TIER 5",
        "#str_42073": "Permanently clears Arena wins, best results and unlocks. Mission saves and multiplayer settings are not affected.",
        "#str_42165": "NEW BEST",
        "#str_42166": "No result yet",
        "#str_42200": "Deathmatch",
        "#str_42201": "Duel",
        "#str_42202": "Team Deathmatch",
        "#str_42203": "Red Rover",
        "#str_42204": "Clan Arena",
    }
    english = languages["english"]
    for key, expected in english_expected.items():
        if english[key] != expected:
            raise AssertionError(
                f"English {key} is {english[key]!r}, expected {expected!r}"
            )

    for locale in ("french", "italian", "spanish"):
        translated = sum(
            languages[locale][key] != english[key] for key in required_ids
        )
        if translated < 40:
            raise AssertionError(
                f"{locale} arena table looks untranslated ({translated} distinct values)"
            )


def validate_menu_hooks() -> None:
    main_menu = read(MAIN_MENU)
    arena_menu = read(ARENA_MENU)
    mp_hud = read(ROOT / "content" / "baseoq4" / "pak0" / "guis" / "mphud.gui")
    session_menu = read(ROOT / "src" / "framework" / "Session_menu.cpp")

    for needle in (
        'set "desktop::dest" "37" ;',
        'resettime "anim_mainOut" "0" ;',
        'set "cmd" "singlePlayerOpen" ;',
        "onNamedEvent returnFromSinglePlayer",
        "singleplayer_topbar_home",
        "singleplayer_btmbar_home",
    ):
        require(main_menu, needle, "animated Single Player selector entry/return")
    require(
        main_menu,
        'set "cmd" "play main_menu_selection ; startMap game/airdefense1" ;',
        "unchanged Mission start",
    )
    for command in (
        "arenaCampaigns",
        "arenaBrowse",
        "arenaClose",
        "arenaBackToModes",
        "arenaDifficulty -1",
        "arenaDifficulty 1",
        "arenaReset",
        "arenaResetConfirm",
        "arenaResetCancel",
        "arenaStart",
        "arenaRetry",
        "arenaDismissResult",
    ):
        require(arena_menu, command, "Arena Campaign GUI commands")
    for tier in range(5):
        require(
            arena_menu,
            f"arenaSelectTier {tier}",
            "Arena Campaign tier buttons",
        )
    for match in range(4):
        require(
            arena_menu,
            f"arenaSelectMatch {match}",
            "Arena Campaign match buttons",
        )
    for string_id in (
        "#str_42000",
        "#str_230038",
        "#str_42002",
        "#str_42009",
        "#str_42010",
        "#str_42011",
        "#str_42026",
        # The Single Player page: its description and its sections' summaries.
        "#str_230044",
        "#str_231552",
        "#str_231553",
        "#str_42032",
        "#str_42033",
        "#str_42034",
        "#str_42049",
        "#str_42070",
        "#str_42071",
        "#str_42072",
        "#str_42073",
        "#str_42074",
        "#str_42075",
        "#str_42076",
        "#str_42079",
        "#str_42080",
        "#str_42081",
        "#str_42082",
        "#str_42083",
        "#str_42084",
        "#str_42085",
        "#str_42086",
        "#str_42087",
        "#str_42088",
        "#str_42089",
        "#str_42090",
        "#str_42091",
        "#str_42092",
        "#str_42093",
        "#str_42094",
        "#str_42095",
    ):
        require(arena_menu + mp_hud, string_id, "Arena Campaign GUI localization")

    for state_name in (
        "arenaCampaignProgress",
        "arena_resetConfirm",
        "arena_resultCanRetry",
        "arena_selectedTierDescription",
        "arena_selectedTierName",
        "arena_selectedSkill",
        "arena_previousCampaignProgress",
        "arena_progressChanged",
        "arena_resultTier",
        "arena_resultMatch",
        "arena_resultNewlyCompleted",
        "arena_resultUnlockedBoss",
        "arena_resultUnlockedTier",
        "arena_resultUnlockedCampaign",
        "arena_resultMatchName",
    ):
        require(arena_menu, state_name, "Arena Campaign GUI state")
    focus_handler = function_body(arena_menu, "onEvent")
    for focus_target in (
        "arena_reset_cancel",
        "arena_result_next",
        "arena_result_next_full",
        "arena_result_return",
        "arena_result_return_full",
        "arena_result_retry",
        "arena_result_replay",
        "arena_result_continue_only",
        "arena_mode_mission_card",
        "arena_start_enabled",
        "arena_browser_back",
        "arena_transition_blocker",
    ):
        require(
            focus_handler,
            f'setfocus "{focus_target}"',
            "Arena recommended keyboard/controller focus",
        )
    require_regex(
        arena_menu,
        r"windowDef\s+arena_result_draw_accent\s*\{.*?arena_result\"\s*==\s*4",
        "distinct Arena draw presentation",
    )
    require_regex(
        arena_menu,
        r"windowDef\s+arena_match3_locked_name\s*\{.*?arena_match3_locked\"\s*==\s*1.*?#str_42024",
        "locked Arena boss concealment",
    )
    require_regex(
        arena_menu,
        r"windowDef\s+arena_browser_reset_disabled\s*\{.*?arena_canReset\"\s*==\s*0",
        "disabled reset with nothing on the record",
    )

    esc_handler = function_body(arena_menu, "onESC")
    for event_name in (
        "arenaResetCancelTransition",
        "arenaReturnToModes",
        "arenaCloseSelector",
    ):
        require(esc_handler, event_name, "animated Arena Escape/back navigation")
    require(
        esc_handler,
        'resetTime "arena_result_exit_timeline" "0"',
        "animated Arena result Escape/back navigation",
    )

    for event_name in (
        "arenaMatchLaunch",
        "arenaResultReveal",
        "arenaProgressReveal",
        "arenaBrowserReveal",
        "arenaResetReveal",
    ):
        require(
            arena_menu,
            f"onNamedEvent {event_name}",
            "Arena cinematic menu lifecycle",
        )
    for timeline_name in (
        "arena_mode_reveal_timeline",
        "arena_mode_exit_timeline",
        "arena_browser_entry_timeline",
        "arena_browser_exit_timeline",
        "arena_mode_return_timeline",
        "arena_reset_reveal_timeline",
        "arena_reset_exit_timeline",
        "arena_launch_timeline",
        "arena_result_reveal_timeline",
        "arena_result_exit_timeline",
        "arena_progress_reveal_timeline",
        "arena_browser_reveal_timeline",
    ):
        require(arena_menu, timeline_name, "Arena cinematic menu animation")
    for overlay_name in (
        "arena_launch_entry_curtain",
        "arena_launch_exit_curtain",
        "arena_progress_entry_curtain",
        "arena_progress_exit_curtain",
        "arena_reset_entry_curtain",
        "arena_reset_exit_curtain",
        "arena_result_curtain",
    ):
        require(arena_menu, overlay_name, "Arena layered modal exit")
    for material in (
        "gfx/guis/mainmenu/popup_bg",
        "gfx/guis/mainmenu/popup_top",
        "gfx/guis/mainmenu/popup_mid",
        "gfx/guis/mainmenu/popup_btm",
        "gfx/guis/loading/bg_grid_tiled",
        "gfx/guis/mainmenu/topbar_tile_left",
        "gfx/guis/mainmenu/topbar_tile_right",
        "gfx/guis/mainmenu/btmbar_tile_left",
        "gfx/guis/mainmenu/btmbar_tile_right",
    ):
        require(arena_menu, material, "Arena shared legacy popup treatment")
    for state_name in ("arena_navigationAnimating", "arena_resetAnimating"):
        require(arena_menu, state_name, "Arena transition input blocking")
    require(
        arena_menu,
        '"gui::arena_resultMatchName"',
        "Arena post-match identity review",
    )
    require(
        arena_menu,
        '"gui::arenaCampaignProgress" - "gui::arena_previousCampaignProgress"',
        "Arena visible campaign progress delta",
    )
    require(
        arena_menu,
        "arena_progress_boss_pulse",
        "Arena newly unlocked boss-card reveal",
    )
    require(arena_menu, "arena_result_curtain", "Arena wipe-to-report continuity curtain")
    require(
        arena_menu,
        'set "Desktop::arena_launchVisible" "0"',
        "stale Arena launch-card cleanup",
    )
    require(
        arena_menu,
        "backcolor\t0.015,0.022,0.014,0.88",
        "legible Arena result ledger",
    )
    for needle in (
        'set "arena_result_panel::rect" "320,240,0,0" ;',
        'transition "arena_result_panel::rect" "320,240,0,0" "122,91,396,298" "520" ;',
        'set "arena_progress_update_panel::rect" "320,231,0,0" ;',
        'transition "arena_progress_update_panel::rect" "320,231,0,0" "110,169,420,124" "480" ;',
        "arena_reset_shell",
        'transition "arena_reset_shell::rect" "320,233,0,0" "140,33,360,400" "260" ;',
    ):
        require(arena_menu, needle, "two-axis Arena popup expansion")

    for event_name in (
        "arenaCampaignEntrance",
        "arenaCampaignVictory",
        "arenaCampaignPresentationClear",
    ):
        require(mp_hud, f"onNamedEvent {event_name}", "Arena in-world presentation HUD")
    for state_name in (
        "arena_presentTitle",
        "arena_presentSubtitle",
        "arena_presentVictor",
        "arena_presentHasVictor",
        "arena_presentScore",
    ):
        require(mp_hud, state_name, "Arena in-world presentation HUD state")
    # A tie for the lead leaves no unique victor even though the outcome is not
    # a draw, so the champion card must follow the name and not the outcome or
    # it renders "ARENA CHAMPION" over an empty line.
    if '"gui::arena_presentOutcome"' in mp_hud:
        raise AssertionError(
            "Arena champion card must gate on arena_presentHasVictor, not the outcome"
        )
    if mp_hud.count('"gui::arena_presentHasVictor" == 1') != 3:
        raise AssertionError(
            "Arena champion card, label and name must all gate on a named victor"
        )
    require(mp_hud, "oq4_arena_present_victory", "Arena victory presentation timeline")
    for timer_rect in ("23,59,200,20", "23,96,200,20", "23,94,200,20"):
        require(mp_hud, timer_rect, "readable localized Arena entrance clock")
    if '"gui::arena_presentVictor" != ""' in mp_hud:
        raise AssertionError("Arena HUD visibility must not depend on numeric GUI string comparison")

    if arena_menu.count('"gui::virtual_screen_x_expand"') < 4:
        raise AssertionError("Arena modal backdrops must cover widescreen expansion")
    if arena_menu.count('"gui::virtual_screen_y_expand"') < 4:
        raise AssertionError("Arena modal backdrops must cover vertical expansion")
    require_regex(
        arena_menu,
        r"windowDef\s+arena_screen_tint\s*\{.*?virtual_screen_x_expand.*?virtual_screen_y_expand",
        "Arena screen tint must cover both expanded axes",
    )
    text_scales = [
        float(value)
        for value in re.findall(r"\btextscale\s+([0-9]+(?:\.[0-9]+)?)", arena_menu)
    ]
    if not text_scales or min(text_scales) < 0.13:
        raise AssertionError("Arena GUI contains unreadably small authored text")

    require(
        session_menu,
        'if ( !idStr::Icmp( cmd, "singlePlayerOpen" ) )',
        "Single Player selector dispatch",
    )
    require(
        session_menu,
        "arenaCampaign.OpenSelector();",
        "Single Player selector dispatch",
    )
    require(
        session_menu,
        "} else if ( arenaCampaign.IsGui( gui ) ) {",
        "Arena Campaign GUI dispatch",
    )
    require(
        session_menu,
        "arenaCampaign.HandleGuiCommand( menuCommand );",
        "Arena Campaign GUI dispatch",
    )


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing function {signature!r}")
    brace = source.find("{", start)
    if brace < 0:
        raise AssertionError(f"Missing body for {signature!r}")
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"Unterminated function {signature!r}")


def gui_window_body(source: str, name: str) -> str:
    """Return one windowDef's own text, brace-matched.

    A regex with a lazy wildcard silently runs past the closing brace and
    matches the next window's properties instead, which makes the assertion
    pass when the property was deleted.
    """

    match = re.search(rf"windowDef\s+{re.escape(name)}\s*(?=\{{)", source)
    if match is None:
        raise AssertionError(f"Missing windowDef {name!r}")
    brace = source.find("{", match.end())
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"Unterminated windowDef {name!r}")


def validate_engine_hooks() -> None:
    arena = read(ROOT / "src" / "framework" / "ArenaCampaign.cpp")
    arena_header = read(ROOT / "src" / "framework" / "ArenaCampaign.h")
    common = read(ROOT / "src" / "framework" / "Common.cpp")
    session = read(ROOT / "src" / "framework" / "Session.cpp")
    session_local = read(ROOT / "src" / "framework" / "Session_local.h")
    async_network = read(ROOT / "src" / "framework" / "async" / "AsyncNetwork.cpp")
    async_client = read(ROOT / "src" / "framework" / "async" / "AsyncClient.cpp")
    draw_common = read(ROOT / "src" / "renderer" / "draw_common.cpp")
    vulkan_gui = read(ROOT / "src" / "renderer" / "Vulkan" / "vk_GuiExecutor.cpp")
    vulkan_blur_shader = read(
        ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "material_depth_aware_blur.frag"
    )

    smooth_slow_time = function_body(
        common,
        "static bool openQ4_ShouldUseSmoothSingleplayerSlowTime( float effectiveTimeScale )",
    )
    require(
        common,
        "static std::atomic<bool> openQ4_singleplayerGameModuleReady( false );",
        "Arena-safe async module state",
    )
    require(
        smooth_slow_time,
        "openQ4_singleplayerGameModuleReady.load( std::memory_order_acquire )",
        "Arena-safe async module transition",
    )
    require(
        smooth_slow_time,
        "return effectiveTimeScale < 0.999f;",
        "Composed single-player slow-time detection",
    )
    if (
        "cvarSystem->" in smooth_slow_time
        or "GetCVarString" in smooth_slow_time
        or "com_activeGameModule" in smooth_slow_time
    ):
        raise AssertionError(
            "async slow-time detection must not read mutable CVar/module strings during a module swap"
        )

    # Arena's victor-focus DOF uses Raven's normalized controller contract.
    # GL consumes normalized depth directly; Vulkan converts focus/range to
    # world distance but must linearize its raw depth with the same historical
    # special-depth near-plane convention, not parm 7's distance scale.
    gl_blur = function_body(draw_common, "static bool RB_CompositeRVSpecialBlur( void )")
    require(
        gl_blur,
        "idMath::ClampFloat( 0.0f, 1.0f, rbRVSpecialEffects.parms[ SPECIAL_EFFECT_BLUR ][5] )",
        "OpenGL normalized special-blur focus",
    )
    vk_blur = function_body(
        vulkan_gui, "static bool VK_Exec_DrawRVSpecialBlur( const viewDef_t *viewDef )"
    )
    require(
        vulkan_gui,
        "VK_RVSPECIAL_DEPTH_ZNEAR = 0.25f",
        "Vulkan Raven special-depth convention",
    )
    require(
        vk_blur,
        "vkExec.pendingSpecialEffects.parms[ SPECIAL_EFFECT_BLUR ][ 5 ] )\n\t\t\t\t\t* distanceScale",
        "Vulkan normalized-focus translation",
    )
    require(
        vk_blur,
        "parms[ 5 ][ 0 ] = VK_RVSPECIAL_DEPTH_ZNEAR;",
        "Vulkan raw-depth linearization near plane",
    )
    require(
        vulkan_blur_shader,
        "5 specialDepthZNear (controller distance scale is consumed at the C++ boundary)",
        "Vulkan special-blur shader parameter contract",
    )
    require(
        vulkan_blur_shader,
        "writes the special-depth near plane here after consuming distanceScale",
        "Vulkan special-blur shader translated near-plane input",
    )
    if "parms[ 5 ][ 0 ] = distanceScale;" in vk_blur:
        raise AssertionError(
            "Vulkan special blur must not use the controller distance scale as zNear"
        )
    load_game_dll = function_body(common, "void idCommonLocal::LoadGameDLL( void )")
    unload_game_dll = function_body(common, "void idCommonLocal::UnloadGameDLL( void )")
    shutdown_game = function_body(common, "void idCommonLocal::ShutdownGame( bool reloading )")
    require(
        load_game_dll,
        "openQ4_singleplayerGameModuleReady.store( false, std::memory_order_release );",
        "Arena-safe module load transition",
    )
    require(
        load_game_dll,
        'game != NULL && idStr::Icmp( gameModuleBaseName, "game_sp" ) == 0,',
        "Arena-safe module ready transition",
    )
    require(
        shutdown_game,
        "openQ4_singleplayerGameModuleReady.store( false, std::memory_order_release );",
        "Arena-safe module shutdown transition",
    )
    if "game->Shutdown();" in unload_game_dll:
        raise AssertionError("Arena binary-only module unload still shuts down the game object")

    for token in (
        'ARENA_CAMPAIGN_FILE = "arena/openq4_campaign.cfg"',
        "ARENA_TIER_COUNT = 5",
        "ARENA_MATCHES_PER_TIER = 4",
        "ARENA_BOSS_MATCH = ARENA_MATCHES_PER_TIER - 1",
        "ARENA_PROGRESS_MASK",
        "ARENA_OUTCOME_DRAW = 2",
        "ARENA_ENTRANCE_MSEC = 1250",
        "enum arenaTransitionPhase_t",
        "ARENA_TRANSITION_ENTRANCE",
        "ARENA_TRANSITION_RETURN",
        "ARENA_TRANSITION_RESULT",
        '"ui_arenaProgress", "0"',
        "CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER",
        "CVAR_SYSTEM | CVAR_BOOL | CVAR_NOCHEAT",
        "CVAR_SYSTEM | CVAR_INTEGER | CVAR_NOCHEAT",
        "static bool ArenaTierUnlocked",
        "static bool ArenaBossUnlocked",
        "static int ArenaCanonicalProgress",
        "ArenaRepairPersistedProgress",
        "match < ARENA_BOSS_MATCH || ArenaBossUnlocked",
        "LoadCampaign()",
        "bool campaignClosed = false",
        "lexer.HadError() || !campaignClosed",
        'uiManager->FindGui( "guis/arena_menu.gui"',
        "ArenaMapSupportsMatch",
        'if ( !idStr::Icmp( cmd, "arenaMission" ) )',
        'mainMenu->HandleNamedEvent( "openMission" )',
        'if ( !idStr::Icmp( cmd, "arenaBrowse" ) )',
        'va( "addbot \\"%s\\" %d exact\\n"',
        "match->baseSkill + difficulty - 3 + match->bots[i].skillOffset",
        'va( "spawnServer \\"%s\\"\\n"',
        "ArenaMarkUniqueField",
        "MATCH_REQUIRED_FIELDS",
        "ArenaStringIsLocalizationKey",
        "match.bots.Num() & 1",
        "duplicate bot",
        "ArenaSaveCurrentCVars",
        "ArenaRestoreSavedCVars",
        "resetCVars",
        'va( "reset %s\\n", name )',
        'idStr::Icmp( name, "si_pure" ) == 0',
        "clientsBeforePopulation",
        "clientsBeforePopulation != 1",
        "idAsyncNetwork::server.GetNumClients()",
        "botsAdded != match->bots.Num()",
        'state->result = report ? 3 : 0',
        'state->resultFailure = report ? failure : ARENA_FAILURE_NONE;',
        '"si_minPlayers"',
        'ArenaSetMatchCVar( "si_minPlayers", 2 )',
        'cvarSystem->SetCVarBool( "si_isBuyingEnabled", false )',
        'cvarSystem->SetCVarBool( "si_dropWeaponsInBuyingModes", false )',
        'cvarSystem->SetCVarBool( "si_allowVoting", false )',
        'cvarSystem->SetCVarBool( "si_teamDamage", false )',
        'cvarSystem->SetCVarBool( "si_weaponStay", false )',
        'ArenaSetMatchCVar( "si_overtime", 120 )',
        'ArenaSetMatchCVar( "si_fps", 60 )',
        '"ui_autoJoin"',
        'cvarSystem->SetCVarBool( "ui_autoJoin", true )',
        'cvarSystem->SetCVarString( "ui_spectate", "Play" )',
        'ArenaSetMatchCVar( "si_arenaCampaign", token )',
        "progress |= ArenaMatchBit( tier, match )",
        'state->resultUnlock = "#str_42047"',
        'state->resultUnlock = "#str_42046"',
        'state->resultUnlock = "#str_42045"',
        "const bool newlyCompleted",
        "!ArenaBossUnlocked( previousProgress, tier )",
        'if ( !idStr::Icmp( cmd, "arenaRetry" ) )',
        'if ( !idStr::Icmp( cmd, "arenaResetConfirm" ) )',
        'if ( !idStr::Icmp( cmd, "arenaResetCancel" ) )',
        '"arena_selectedTierDescription"',
        '"arena_selectedSkill"',
        '"arena_resultCanRetry"',
        '"arena_resetConfirm"',
        'cmdSystem->RemoveCommand( "arenaCampaignFinish" )',
        'cmdSystem->BufferCommandText( CMD_EXEC_INSERT, "arenaCampaignFinish\\n" )',
        '"arena campaign: accepted result token %d',
        "moduleTransitionPending",
        'state->result == 4 ? ArenaLocalized( "#str_42077" )',
        'state->result == 4 ? ArenaLocalized( "#str_42078" )',
        'HandleNamedEvent( "arenaMatchLaunch" )',
        'HandleNamedEvent( "arenaResultReveal" )',
        '"arenaProgressReveal"',
        '"arenaBrowserReveal"',
        '"arena_transitionPhase"',
        '"arena_transitionRemainingMsec"',
        '"arena_previousCampaignProgress"',
        '"arena_progressChanged"',
        '"arena_resultTier"',
        '"arena_resultMatch"',
        '"arena_resultNewlyCompleted"',
        '"arena_resultUnlockedBoss"',
        '"arena_resultUnlockedTier"',
        '"arena_resultUnlockedCampaign"',
        '"arena_resultTierName"',
        '"arena_resultMatchName"',
    ):
        require(arena, token, "Arena Campaign framework")

    saved_cvars_match = re.search(
        r"ARENA_SAVED_CVARS\[\]\s*=\s*\{(?P<body>.*?)\bNULL\s*\n\s*\};",
        arena,
        re.DOTALL,
    )
    if saved_cvars_match is None:
        raise AssertionError("Could not inspect Arena overridden-CVar snapshot list")
    saved_cvars = set(re.findall(r'"([A-Za-z0-9_]+)"', saved_cvars_match.group("body")))
    prepare_body = function_body(arena, "bool idArenaCampaign::PrepareServer()")
    for saved_rule_cvar in ("g_matchProfile", "si_warmupReadyPercentage"):
        if saved_rule_cvar not in saved_cvars:
            raise AssertionError(
                f"Arena must restore the rule-boundary CVar {saved_rule_cvar!r}"
            )

    save_current_cvars = prepare_body.find("ArenaSaveCurrentCVars( state );")
    casual_profile = prepare_body.find(
        'cvarSystem->SetCVarString( "g_matchProfile", "casual" );'
    )
    find_profile = prepare_body.find(
        'idCVar *matchProfile = cvarSystem->Find( "g_matchProfile" );'
    )
    refresh_profile = prepare_body.find("matchProfile->SetModified();")
    first_match_override = prepare_body.find(
        'cvarSystem->SetCVarInteger( "net_serverDedicated", 0 );'
    )
    if not (
        0
        <= save_current_cvars
        < casual_profile
        < find_profile
        < refresh_profile
        < first_match_override
    ):
        raise AssertionError(
            "Arena must save, force, and explicitly refresh its casual match profile "
            "before importing per-card rules"
        )
    readiness_disabled = prepare_body.find(
        'cvarSystem->SetCVarBool( "si_useReady", false );'
    )
    readiness_threshold_zeroed = prepare_body.find(
        'ArenaSetMatchCVar( "si_warmupReadyPercentage", 0 );'
    )
    if not 0 <= readiness_disabled < readiness_threshold_zeroed:
        raise AssertionError(
            "Arena must pair disabled readiness with a zero ready threshold before rule import"
        )
    overridden_cvars = set(
        re.findall(
            r'(?:ArenaSetMatchCVar|cvarSystem->SetCVar(?:Bool|Float|Integer|String))'
            r'\(\s*"([A-Za-z0-9_]+)"',
            prepare_body,
        )
    )
    unsaved_overrides = overridden_cvars - saved_cvars - {"si_arenaCampaign"}
    if unsaved_overrides:
        raise AssertionError(
            f"Arena match overrides are not restored: {sorted(unsaved_overrides)}"
        )

    for forbidden_clamp in (
        "match.baseSkill = idMath::ClampInt",
        "match.fragLimit = idMath::ClampInt",
        "match.timeLimit = idMath::ClampInt",
        "match.roundLimit = idMath::ClampInt",
        "match.scoreLimit = idMath::ClampInt",
        "bot.skillOffset = idMath::ClampInt",
    ):
        if forbidden_clamp in arena:
            raise AssertionError(
                f"Arena manifest must reject authored range errors, found {forbidden_clamp!r}"
            )

    difficulty_body = function_body(
        arena, "static const char *ArenaDifficultyStringKey"
    )
    difficulty_ids = re.findall(r"#str_\d+", difficulty_body)
    if difficulty_ids != [
        "#str_42060",
        "#str_42061",
        "#str_42062",
        "#str_42063",
        "#str_42064",
    ]:
        raise AssertionError(
            f"Arena difficulty localization map drifted: {difficulty_ids}"
        )

    game_type_body = function_body(
        arena, "static const char *ArenaGameTypeStringKey"
    )
    expected_game_type_ids = {
        "DM": "#str_42200",
        "Duel": "#str_42201",
        "Team DM": "#str_42202",
        "Red Rover": "#str_42203",
        "Clan Arena": "#str_42204",
    }
    for game_type, string_id in expected_game_type_ids.items():
        require_regex(
            game_type_body,
            rf'Icmp\(\s*"{re.escape(game_type)}"\s*\).*?return\s+"{string_id}"',
            f"{game_type} localization mapping",
        )

    tier_unlock_body = function_body(arena, "static bool ArenaTierUnlocked")
    require(
        tier_unlock_body,
        "tier == 0 || ArenaMatchComplete( progress, tier - 1, ARENA_BOSS_MATCH )",
        "boss win unlocks the next tier",
    )
    boss_unlock_body = function_body(arena, "static bool ArenaBossUnlocked")
    require(
        boss_unlock_body,
        "match < ARENA_BOSS_MATCH",
        "first three matches gate the boss",
    )
    require(
        boss_unlock_body,
        "!ArenaMatchComplete( progress, tier, match )",
        "every preliminary win gates the boss",
    )

    canonical_progress_body = function_body(
        arena, "static int ArenaCanonicalProgress"
    )
    for token in (
        "progress &= ARENA_PROGRESS_MASK",
        "!ArenaMatchComplete( canonical, tier - 1, ARENA_BOSS_MATCH )",
        "qualifiersComplete = false",
        "qualifiersComplete && ArenaMatchComplete",
    ):
        require(canonical_progress_body, token, "canonical Arena progression repair")

    def canonical_progress(progress: int) -> int:
        progress &= (1 << 20) - 1
        canonical = 0
        for tier in range(5):
            if tier > 0:
                previous_boss = 1 << (tier * 4 - 1)
                if not canonical & previous_boss:
                    break
            qualifiers = 0b111 << (tier * 4)
            canonical |= progress & qualifiers
            boss = 1 << (tier * 4 + 3)
            if canonical & qualifiers == qualifiers and progress & boss:
                canonical |= boss
        return canonical

    progress_repair_cases = {
        0: 0,
        1 << 3: 0,
        0b0111: 0b0111,
        0b1111: 0b1111,
        1 << 4: 0,
        0b1111 | (1 << 4): 0b1111 | (1 << 4),
        0b1111 | (1 << 7): 0b1111,
        0xFF | (1 << 8): 0xFF | (1 << 8),
        (1 << 20) - 1: (1 << 20) - 1,
        (1 << 24) | 0b1111: 0b1111,
    }
    for persisted, expected in progress_repair_cases.items():
        actual = canonical_progress(persisted)
        if actual != expected:
            raise AssertionError(
                "Arena progress repair drifted: "
                f"0x{persisted:x} became 0x{actual:x}, expected 0x{expected:x}"
            )

    load_body = function_body(arena, "bool idArenaCampaign::LoadCampaign()")
    require(
        load_body,
        "ArenaRepairPersistedProgress();",
        "same-version Arena saves are canonicalized on load",
    )
    shutdown_body = function_body(arena, "void idArenaCampaign::Shutdown()")
    require(
        shutdown_body,
        "state->moduleTransitionPending",
        "only the intended game_sp to game_mp reload preserves an Arena launch",
    )
    abort_body = function_body(arena, "void idArenaCampaign::AbortMatch()")
    require(
        abort_body,
        "state->moduleTransitionPending = false;",
        "Arena abort clears pending module-transition state",
    )
    require(
        abort_body,
        "if ( state->result != 0 )",
        "Arena abort detects an unfinished result presentation",
    )
    require(
        abort_body,
        "ArenaClearResultMetadata( state );",
        "Arena abort cannot strand an unrevealed result overlay",
    )
    for token in (
        "state->completionPending = false;",
        "state->pendingToken = 0;",
        "state->botsPopulated = false;",
        "state->moduleTransitionPending = false;",
        "state->transitionPhase = ARENA_TRANSITION_IDLE;",
        "state->transitionDeadlineMsec = 0;",
        "state->launchToken = 0;",
        "state->resultRevealPending = false;",
        "ui_arenaCampaignActive.SetBool( false );",
        "ui_arenaActiveMatch.SetInteger( 0 );",
        "ArenaRestoreSavedCVars( state );",
    ):
        require(abort_body, token, "atomic Arena cancellation cleanup")
    for token in (
        "if ( state->returnWipeHeld )",
        "sessLocal.ClearWipe();",
        "state->returnWipeHeld = false;",
    ):
        require(abort_body, token, "Arena cancellation releases a held return wipe")
    for signature in (
        "void idArenaCampaign::RequestResetProgress()",
        "void idArenaCampaign::ResetProgress()",
    ):
        reset_body = function_body(arena, signature)
        require(
            reset_body,
            "ui_arenaProgress.GetInteger() == 0",
            "zero-progress Arena reset guard",
        )
    request_reset_body = function_body(
        arena, "void idArenaCampaign::RequestResetProgress()"
    )
    require(
        request_reset_body,
        'HandleNamedEvent( "arenaResetReveal" )',
        "event-driven Arena reset reveal",
    )
    queue_body = function_body(
        arena, "bool idArenaCampaign::QueueCompletion( const idCmdArgs &args )"
    )
    # The parse and the acceptance are shared with a result kept when the match
    # is left during its ceremony (arena_decided_result.py).
    require(
        queue_body,
        "ArenaParseCompletion( args, token, outcome, playerScore, opponentScore, awards )",
        "the handoff uses the shared strict parser",
    )
    require(
        queue_body,
        "AcceptCompletion( token, outcome, playerScore, opponentScore, awards );",
        "the handoff uses the shared acceptance",
    )
    queue_body += function_body(arena, "static bool ArenaParseCompletion(")
    queue_body += function_body(arena, "void idArenaCampaign::AcceptCompletion(")
    for token in (
        # The awards argument is optional on purpose: a game_mp that sends it
        # against a framework that did not expect it would otherwise take the
        # malformed-command path, which fails closed by fabricating a token-0
        # loss and destroying a legitimate result.
        "args.Argc() == 5 || args.Argc() == 6",
        "ArenaParseBoundedInteger",
        "ARENA_OUTCOME_DRAW",
        "state->pendingOutcome = outcome",
        "ARENA_AWARD_MASK, awards",
    ):
        require(queue_body, token, "strict Arena completion handoff")
    if "atoi(" in queue_body:
        raise AssertionError("Arena completion handoff must not accept partial integers")
    require(
        queue_body,
        "QueueFailedCompletionCleanup();",
        "invalid active Arena result fails closed",
    )
    failed_completion = function_body(
        arena, "void idArenaCampaign::QueueFailedCompletionCleanup()"
    )
    for token in (
        "!IsActive()",
        "state->completionPending = true;",
        "state->pendingToken = 0;",
        'BufferCommandText( CMD_EXEC_INSERT, "arenaCampaignFinish\\n" )',
    ):
        require(failed_completion, token, "deferred invalid-result cleanup")

    arena_frame = function_body(arena, "void idArenaCampaign::Frame()")
    for token in (
        "state->transitionPhase == ARENA_TRANSITION_RETURN",
        "state->completionPending",
        "FinishPendingCompletion();",
        "state->transitionPhase != ARENA_TRANSITION_ENTRANCE",
    ):
        require(arena_frame, token, "Arena transition frame orchestration")
    result_finish = arena_frame.find("FinishPendingCompletion();")
    entrance_commit = arena_frame.find("CommitPendingMatch();")
    if not 0 <= result_finish < entrance_commit:
        raise AssertionError(
            "Arena result return must finish before considering an entrance commit"
        )
    require(arena_frame, "CommitPendingMatch();", "Arena entrance fallback commit")

    start_match = function_body(arena, "void idArenaCampaign::StartSelectedMatch()")
    for token in (
        "state->transitionPhase = ARENA_TRANSITION_ENTRANCE;",
        "state->transitionDeadlineMsec = Sys_Milliseconds() + ARENA_ENTRANCE_MSEC;",
        'HandleNamedEvent( "arenaMatchLaunch" )',
    ):
        require(start_match, token, "Arena entrance orchestration")
    if "spawnServer" in start_match:
        raise AssertionError("Arena entrance must complete before spawnServer is queued")

    commit_match = function_body(arena, "void idArenaCampaign::CommitPendingMatch()")
    for token in (
        "ArenaMatchForToken( state, token, &tier, &matchIndex )",
        "state->transitionPhase = ARENA_TRANSITION_MATCH;",
        "ui_arenaCampaignActive.SetBool( true );",
        'va( "spawnServer \\"%s\\"\\n", match->map.c_str() )',
    ):
        require(commit_match, token, "Arena entrance commit")

    open_browser = function_body(arena, "void idArenaCampaign::OpenBrowser()")
    open_selector = function_body(arena, "void idArenaCampaign::OpenSelector()")
    # The browser presents the retained Arena page where it can, else its own.
    require(open_browser, "idUserInterface *view = sessLocal.RetainedArenaPage();", "retained Arena page presentation")
    for name, body, view, present in (
        ("selector", open_selector, "state->view = 0;", "sessLocal.SetGUI( state->gui, NULL );"),
        ("browser", open_browser, "state->view = 1;", "sessLocal.SetGUI( view, NULL );"),
    ):
        view_index = body.find(view)
        update_index = body.find("UpdateGui();", view_index)
        set_gui_index = body.find(present, view_index)
        if not 0 <= view_index < update_index < set_gui_index:
            raise AssertionError(
                f"Arena {name} GUI state must be published before synchronous activation"
            )
    update_before_reveal = open_browser.find("UpdateGui();")
    result_reveal = open_browser.find('HandleNamedEvent( "arenaResultReveal" )')
    if not 0 <= update_before_reveal < result_reveal:
        raise AssertionError("Arena result reveal must fire after post-match GUI state is published")

    finish_completion = function_body(
        arena, "void idArenaCampaign::FinishPendingCompletion()"
    )
    # The recording is its own step, shared with a result kept on quit.
    record_completion = function_body(
        arena, "bool idArenaCampaign::RecordPendingCompletion()"
    )
    require(finish_completion, "if ( !RecordPendingCompletion() ) {", "the result is recorded before the return")
    for token in (
        "state->resultPreviousProgress = previousProgress;",
        "state->resultTier = tier;",
        "state->resultMatch = match;",
        "state->resultNewlyCompleted = newlyCompleted;",
        "state->resultTierName = state->tiers[tier].name;",
        "state->resultMatchName = completedMatch->name;",
    ):
        require(record_completion, token, "Arena cinematic result return")
    for token in (
        "state->transitionPhase = ARENA_TRANSITION_RESULT;",
        'sessLocal.StartWipe( "gfx/wipes/fade", true );',
        "sessLocal.CompleteWipe();",
    ):
        require(finish_completion, token, "Arena cinematic result return")
    wipe_capture = finish_completion.find('sessLocal.StartWipe( "gfx/wipes/fade", true );')
    wipe_complete = finish_completion.find("sessLocal.CompleteWipe();", wipe_capture)
    stop_server = finish_completion.find("sessLocal.StopPreservingWipe();", wipe_complete)
    clear_server_token = finish_completion.find(
        'cvarSystem->SetCVarInteger( "si_arenaCampaign", 0 );', stop_server
    )
    open_result_browser = finish_completion.find("OpenBrowser();", clear_server_token)
    clear_return_wipe = finish_completion.find("sessLocal.ClearWipe();", open_result_browser)
    if not (
        0
        <= wipe_capture
        < wipe_complete
        < stop_server
        < clear_server_token
        < open_result_browser
        < clear_return_wipe
    ):
        raise AssertionError(
            "Arena return must hold black through teardown/result setup, then release it to the GUI"
        )

    stop_internal = function_body(
        session, "void idSessionLocal::StopInternal( bool preserveWipe )"
    )
    for token in (
        "const bool outermostStop = stopDepth == 0;",
        "preserveWipeDuringStop = preserveWipe;",
        "if ( !preserveWipeDuringStop )",
        "ClearWipe();",
        "const auto finishStopPolicy",
        "catch( ... )",
        "finishStopPolicy();",
    ):
        require(stop_internal, token, "nested-safe session wipe preservation")
    require(
        session_local,
        "void\t\t\t\tStopPreservingWipe();",
        "Arena return wipe session interface",
    )

    dismiss_result = function_body(arena, "void idArenaCampaign::DismissResult()")
    update_before_progress = dismiss_result.find("UpdateGui();")
    progress_reveal = dismiss_result.find('"arenaProgressReveal"')
    browser_reveal = dismiss_result.find('"arenaBrowserReveal"')
    if not 0 <= update_before_progress < progress_reveal < browser_reveal:
        raise AssertionError("Arena progression reveal must fire after the result panel clears")
    for forbidden in (
        "state->resultToken = 0;",
        "state->resultTier = -1;",
        "state->resultMatch = -1;",
        "state->resultTierName.Clear();",
        "state->resultMatchName.Clear();",
    ):
        if forbidden in dismiss_result:
            raise AssertionError("DismissResult must preserve completed-match metadata")

    # UpdateGui publishes the browser's state into the stock browser and, while
    # it is loaded, the retained Arena page alike (PublishGui).
    update_gui = function_body(arena, "void idArenaCampaign::UpdateGui()")
    for token in ("PublishGui( state->gui );", "idUserInterface *page = sessLocal.RetainedArenaGui();", "PublishGui( page );"):
        require(update_gui, token, "Arena state published to both browsers")
    update_body = function_body(arena, "void idArenaCampaign::PublishGui( idUserInterface *gui )")
    require(
        update_body,
        'SetStateInt( "arenaCampaignProgress"',
        "Arena Campaign progress-bar state",
    )
    for string_id in (
        "#str_42012",
        "#str_42022",
        "#str_42023",
        "#str_42030",
        "#str_42031",
        "#str_42045",
        "#str_42048",
        "#str_42065",
        "#str_42066",
        "#str_42067",
        # The aborted/abandoned title and body are chosen by reason in
        # ArenaFailureTitleKey/ArenaFailureMessageKey rather than inlined here;
        # validate_failure_reporting pins each one.
        "#str_42077",
        "#str_42078",
        # A replayed win that changed nothing must not promise new unlocks.
        "#str_42164",
    ):
        require(update_body, string_id, "Arena Campaign GUI state mapping")
    require(
        update_body,
        'state->resultNewlyCompleted ? "#str_42066" : "#str_42164"',
        "a replayed win reads as confirmed, not as newly opening the circuit",
    )
    require(
        update_body,
        'ArenaFailureTitleKey( state->resultFailure )',
        "the aborted/abandoned ledger title follows the failure kind",
    )
    require(
        update_body,
        'const char *status = !unlocked ? "#str_42022"',
        "locked Arena status",
    )
    require(
        update_body,
        'complete ? "#str_42023"',
        "completed Arena status",
    )
    # Tier statuses live in ArenaTierStatusKey, which adds IN PROGRESS for a
    # part-cleared tier so it no longer reads like one never played.
    require(update_body, "ArenaTierStatusKey( progress, tierIndex )", "tier status helper")
    tier_status = function_body(arena, "static const char *ArenaTierStatusKey( int progress, int tier )")
    for key in ('"#str_42022"', '"#str_42023"', '"#str_42045"', '"#str_42039"', '"#str_42065"'):
        require(tier_status, key, "tier statuses: locked, complete, boss ready, in progress, available")
    require(
        update_body,
        'state->result == 1 ? ArenaLocalized( "#str_42030" )',
        "Arena victory title",
    )
    require(
        update_body,
        'state->result == 2 ? ArenaLocalized( "#str_42031" )',
        "Arena defeat title",
    )
    require(
        update_body,
        'state->result == 1 ? ArenaLocalized( state->resultNewlyCompleted ? "#str_42066" : "#str_42164" )',
        "Arena victory message",
    )
    require(
        update_body,
        'state->result == 2 ? ArenaLocalized( "#str_42067" )',
        "Arena defeat message",
    )
    require(
        update_body,
        'state->result == 3 ? ArenaLocalized( ArenaFailureTitleKey( state->resultFailure ) )',
        "Arena aborted/abandoned-match title",
    )
    failure_title = function_body(arena, "static const char *ArenaFailureTitleKey( int failure )")
    for key in ('"#str_42163"', '"#str_42068"', "ARENA_FAILURE_ABANDONED"):
        require(failure_title, key, "a walked-out match is abandoned; a failed launch or handoff is aborted")
    require(
        update_body,
        'state->result == 3 ? ArenaLocalized( ArenaFailureMessageKey( state->resultFailure ) )',
        "Arena failure message selected by reason",
    )

    for token in (
        "class idArenaCampaign",
        "OpenSelector",
        "StartSelectedMatch",
        "PrepareServer",
        "PopulateBots",
        "QueueCompletion",
        "FinishPendingCompletion",
        "Frame",
        "CommitPendingMatch",
        "AbortMatch",
        "NeedsCleanup",
    ):
        require(arena_header, token, "Arena Campaign public interface")

    for token in (
        "arenaCampaign.Init();",
        "arenaCampaign.Shutdown();",
        "arenaCampaign.AbortMatch();",
    ):
        require(session, token, "Arena Campaign session lifecycle")

    common_frame = function_body(common, "void idCommonLocal::Frame( void )")
    network_frame = common_frame.find("idAsyncNetwork::RunFrame();")
    arena_transition_frame = common_frame.find("arenaCampaign.Frame();")
    network_branch = common_frame.find("if ( idAsyncNetwork::IsActive() )")
    if not 0 <= network_frame < arena_transition_frame < network_branch:
        raise AssertionError(
            "Arena transition frame must run after async results and before the network/session split"
        )

    reload_module = function_body(common, "void Com_ReloadGameModule_f( const idCmdArgs &args )")
    swap_failure = reload_module.find("if ( swapFailed )")
    swap_cleanup = reload_module.find("arenaCampaign.AbortMatch();", swap_failure)
    swap_menu = reload_module.find("session->StartMenu();", swap_failure)
    if not 0 <= swap_failure < swap_cleanup < swap_menu:
        raise AssertionError(
            "failed Arena game-module swaps must restore their transaction before opening the menu"
        )
    post_reload_failure = reload_module.find("if ( !cmdSystem->PostReloadEngine() )")
    abandoned_launch = reload_module.find(
        "const bool abandonedArenaLaunch = arenaCampaign.NeedsCleanup();",
        post_reload_failure,
    )
    post_reload_abort = reload_module.find("arenaCampaign.AbortMatch();", abandoned_launch)
    # The recovery reports why the match never started, as every other failed
    # launch does, rather than reopening the browser as if the click was ignored.
    post_reload_browser = reload_module.find("arenaCampaign.ReportStartFailure();", post_reload_abort)
    if not 0 <= post_reload_failure < abandoned_launch < post_reload_abort < post_reload_browser:
        raise AssertionError(
            "Arena must recover to its browser with a report when the post-swap launch command is lost"
        )

    reliable_messages = function_body(
        async_client, "void idAsyncClient::ProcessReliableServerMessages( void )"
    )
    disconnect_case = reliable_messages.find("case SERVER_RELIABLE_MESSAGE_DISCONNECT")
    reliable_abort = reliable_messages.find("arenaCampaign.AbortMatch();", disconnect_case)
    reliable_stop = reliable_messages.find("session->Stop();", reliable_abort)
    if not 0 <= disconnect_case < reliable_abort < reliable_stop:
        raise AssertionError(
            "server-directed Arena disconnect must restore the campaign before session teardown"
        )
    disconnect_packet = function_body(
        async_client,
        "void idAsyncClient::ProcessDisconnectMessage( const netadr_t from, const idBitMsg &msg )",
    )
    packet_abort = disconnect_packet.find("arenaCampaign.AbortMatch();")
    packet_stop = disconnect_packet.find("session->Stop();", packet_abort)
    if not 0 <= packet_abort < packet_stop:
        raise AssertionError(
            "Arena disconnect packets must restore the campaign before session teardown"
        )
    connect_to_server = function_body(
        async_client, "void idAsyncClient::ConnectToServer( const netadr_t adr )"
    )
    connect_abort = connect_to_server.find("arenaCampaign.AbortMatch();")
    connect_stop = connect_to_server.find("session->Stop();", connect_abort)
    if not 0 <= connect_abort < connect_stop:
        raise AssertionError(
            "connect/reconnect must cancel a pending Arena transaction before replacing its session"
        )

    quit_body = function_body(common, "void idCommonLocal::Quit( void )")
    write_configuration_body = function_body(
        common, "void idCommonLocal::WriteConfiguration( void )"
    )
    require(
        common,
        'com_activeGameModule", "", CVAR_SYSTEM | CVAR_NOCHEAT',
        "active game-module identity survives multiplayer cheat reset",
    )
    require(
        common,
        'com_nextGameModule", "", CVAR_SYSTEM | CVAR_NOCHEAT',
        "pending game-module identity survives multiplayer cheat reset",
    )
    require(
        quit_body,
        "arenaCampaign.AbortMatch();",
        "Arena settings restored before clean shutdown",
    )
    if quit_body.find("arenaCampaign.AbortMatch();") > quit_body.find(
        "WriteConfiguration();"
    ):
        raise AssertionError(
            "Arena clean-quit restore must run before the final configuration write"
        )
    require(
        write_configuration_body,
        "arenaCampaign.NeedsCleanup()",
        "temporary Arena server rules are never serialized",
    )

    for token in (
        "!arenaCampaign.PrepareServer()",
        "arenaCampaign.PopulateBots();",
        '"arenaComplete"',
        "arenaCampaign.QueueCompletion( args );",
    ):
        require(async_network, token, "Arena Campaign async handoff")
    completion_dispatch = function_body(
        async_network, "void idAsyncNetwork::ExecuteSessionCommand( const char *sessCmd )"
    )
    if "atoi( args.Argv" in completion_dispatch:
        raise AssertionError("Async Arena result dispatch must preserve strict parsing")


def validate_game_hooks() -> None:
    sys_cvar = GAME_LIBS_ROOT / "src" / "mpgame" / "gamesys" / "SysCvar.cpp"
    multiplayer = GAME_LIBS_ROOT / "src" / "mpgame" / "MultiplayerGame.cpp"
    multiplayer_header = GAME_LIBS_ROOT / "src" / "mpgame" / "MultiplayerGame.h"
    network_runtime = GAME_LIBS_ROOT / "src" / "mpgame" / "Game_network.cpp"
    sp_multiplayer = GAME_LIBS_ROOT / "src" / "game" / "MultiplayerGame.cpp"
    sp_network_runtime = GAME_LIBS_ROOT / "src" / "game" / "Game_network.cpp"
    bot_runtime = GAME_LIBS_ROOT / "src" / "mpgame" / "bots" / "Bot.cpp"
    duel_runtime = GAME_LIBS_ROOT / "src" / "mpgame" / "mp" / "Duel.cpp"
    state_runtime = GAME_LIBS_ROOT / "src" / "mpgame" / "mp" / "GameState.cpp"
    round_runtime = GAME_LIBS_ROOT / "src" / "mpgame" / "mp" / "RoundGameState.cpp"
    round_modes = GAME_LIBS_ROOT / "src" / "mpgame" / "mp" / "RoundModes.cpp"
    if not (
        sys_cvar.is_file()
        and multiplayer.is_file()
        and multiplayer_header.is_file()
        and network_runtime.is_file()
        and sp_multiplayer.is_file()
        and sp_network_runtime.is_file()
        and bot_runtime.is_file()
        and duel_runtime.is_file()
        and state_runtime.is_file()
        and round_runtime.is_file()
        and round_modes.is_file()
    ):
        print(
            "arena_campaign: skipped GameLibs hook check "
            f"(no companion checkout at {GAME_LIBS_ROOT})"
        )
        return

    cvars = read(sys_cvar)
    game = read(multiplayer)
    header = read(multiplayer_header)
    network = read(network_runtime)
    sp_game = read(sp_multiplayer)
    sp_network = read(sp_network_runtime)
    bots = read(bot_runtime)
    duel = read(duel_runtime)
    state = read(state_runtime)
    rounds = read(round_runtime)
    modes = read(round_modes)
    require(cvars, 'idCVar si_arenaCampaign(', "Arena Campaign server token")
    require(cvars, "CVAR_GAME | CVAR_SERVERINFO | CVAR_INTEGER", "Arena server token flags")
    require(cvars, '"single-player arena campaign match token', "Arena server token purpose")

    for token in (
        "BeginArenaCampaignResult",
        "UpdateArenaCampaignResult",
        'gameLocal.serverInfo.GetInt( "si_arenaCampaign" )',
        "gameLocal.IsTeamGame()",
        "gameLocal.gameType != GAME_REDROVER",
        "arenaResultPlayerScore > arenaResultOpponentScore",
        "ARENA_RESULT_DRAW",
        "ARENA_SCORE_UNAVAILABLE",
        "arenaResultOutcome",
        "arenaResultOpponentScore = 0;",
        'gameLocal.sessionCommand = va( "arenaComplete %d %d %d %d %d"',
        "currentState == GAMEREVIEW",
        '"arena campaign: queued result token %d',
        '"arena campaign: reporting result token %d',
    ):
        require(game, token, "Arena Campaign authoritative result")
    require(header, "BeginArenaCampaignResult", "Arena result declarations")
    require(header, "UpdateArenaCampaignResult", "Arena result declarations")

    result_capture = function_body(
        game, "void idMultiplayerGame::BeginArenaCampaignResult( void )"
    )
    require(
        result_capture,
        "gameLocal.gameType == GAME_DUEL && player->spectating",
        "Arena Duel result excludes waiting queue",
    )

    score_tie = function_body(
        game, "bool idMultiplayerGame::ScoreIsTied( int *leadingScore )"
    )
    require(score_tie, "topScoreCount > 1", "Arena live tie detection")
    require(score_tie, "scoreFound ? topScore : 0", "Arena signed leading score")
    if "GetRankedPlayer" in score_tie:
        raise AssertionError("Arena tie detection must not depend on stale ranked players")

    dm_run = function_body(state, "void rvDMGameState::Run( void )")
    require(dm_run, "ScoreIsTied( &leadingScore )", "Duel/DM authoritative overtime tie")
    require(
        state,
        '"overtime: period %d extended match by %d seconds',
        "Arena overtime diagnostic",
    )

    schedule_round = function_body(
        rounds, "bool rvRoundGameState::ScheduleNextRound( void )"
    )
    prepare_round = schedule_round.find("PrepareNextRound();")
    reset_round = schedule_round.find("ResetRound();")
    if not 0 <= prepare_round < reset_round:
        raise AssertionError("round modes must normalize state before the next respawn")

    rover_prepare = function_body(
        modes, "void rvRedRoverGameState::PrepareNextRound( void )"
    )
    for token in (
        "gameLocal.mpGame.ApplyRoundTeamAssignment( player, targetTeam, false )",
        "targetTeam = seat->side;",
        '"red rover: prepared round %d with %d Marine and %d Strogg players',
    ):
        require(rover_prepare, token, "Red Rover between-round rebalance")

    bot_match_end = function_body(bots, "void rvBotManager::OnMatchEnd( void )")
    require(bot_match_end, "topScoreCount > 1", "Arena bot draw detection")
    require(
        bot_match_end,
        "gameLocal.gameType == GAME_DUEL && player->spectating",
        "Arena bot result excludes Duel waiting queue",
    )
    duel_waiter_skip = bot_match_end.rfind(
        "gameLocal.gameType == GAME_DUEL && player->spectating"
    )
    bot_result_chat = bot_match_end.find("bots[i].OnMatchEnd( won );")
    if not 0 <= duel_waiter_skip < bot_result_chat:
        raise AssertionError("Duel waiting bots must be skipped before result chat")
    require(bot_match_end, "gameLocal.mpGame.ForfeitTeam()", "Arena bot forfeit result")
    require(bot_match_end, "forfeitWinner < 0", "Arena bot true-draw detection")
    require(
        bot_match_end,
        "player->team == forfeitWinner",
        "Arena bot forfeit winner chatter",
    )
    require(bot_match_end, "if ( matchTied )", "Arena bot draw-chat suppression")

    for module, module_game, module_network in (
        ("game_mp", game, network),
        ("game_sp", sp_game, sp_network),
    ):
        message_writer = function_body(
            module_game,
            "void idMultiplayerGame::PrintMessageEvent( int to, msg_evt_t evt, "
            "int parm1, int parm2 )",
        )
        message_reader = function_body(
            module_network,
            "void idGameLocal::ClientProcessReliableMessage( int clientNum, "
            "const idBitMsg &msg )",
        )
        require(
            message_writer,
            "outMsg.WriteChar( parm1 );",
            f"{module} signed multiplayer message parameter write",
        )
        require(
            message_writer,
            "outMsg.WriteChar( parm2 );",
            f"{module} signed multiplayer message parameter write",
        )
        require(
            message_reader,
            "parm1 = msg.ReadChar( );",
            f"{module} signed multiplayer message parameter read",
        )
        require(
            message_reader,
            "parm2 = msg.ReadChar( );",
            f"{module} signed multiplayer message parameter read",
        )
        for unsigned_form in (
            "outMsg.WriteByte( parm1",
            "outMsg.WriteByte( parm2",
            "parm1 = msg.ReadByte",
            "parm2 = msg.ReadByte",
        ):
            if unsigned_form in message_writer or unsigned_form in message_reader:
                raise AssertionError(
                    f"{module} must not encode optional message parameters as unsigned bytes"
                )

    result_update = function_body(
        game, "void idMultiplayerGame::UpdateArenaCampaignResult( void )"
    )
    benign_review_handoff = result_update.find(
        'idStr::Icmp( gameLocal.sessionCommand.c_str(), "game_startmenu" ) == 0'
    )
    unrelated_handoff = result_update.find(
        "because session command '%s' owns the handoff"
    )
    unrelated_handoff_cancel = result_update.find(
        "arenaResultPending = false;", unrelated_handoff
    )
    result_report = result_update.find(
        'gameLocal.sessionCommand = va( "arenaComplete %d %d %d %d %d"'
    )
    if not (
        0
        <= benign_review_handoff
        < unrelated_handoff
        < unrelated_handoff_cancel
        < result_report
    ):
        raise AssertionError(
            "Arena completion must retry the review menu but preserve unrelated handoffs"
        )

    stat_summary = function_body(game, "void idMultiplayerGame::ShowStatSummary( void )")
    campaign_guard = stat_summary.find("if ( arenaCampaignToken > 0 )")
    stock_review_menu = stat_summary.find('gameLocal.sessionCommand = "game_startmenu";')
    if not 0 <= campaign_guard < stock_review_menu:
        raise AssertionError(
            "Arena review must suppress only the stock multiplayer stat-summary command"
        )
    require(
        stat_summary,
        '"arena campaign: suppressed multiplayer stat menu for result token %d',
        "Arena review-menu collision diagnostic",
    )
    require(
        stat_summary,
        '} else {\n\t\tnextMenu = 3;\n\t\tgameLocal.sessionCommand = "game_startmenu";',
        "Stock multiplayer stat summary remains unchanged outside Arena",
    )
    require(
        bots,
        "botCharacterManager.FindCharacter( this->name.c_str() )",
        "Arena exact character roster binding",
    )
    require(
        bots,
        "botCharacterManager.MarkCharacterUsed( character )",
        "Arena character roster claim",
    )
    require(
        bots,
        "player->spectating && player->wantSpectate",
        "Arena bot team balancing ignores only genuine spectators",
    )
    require(
        bots,
        'gameLocal.userInfo[i].GetString( "ui_team" )',
        "Arena bot team balancing counts pending play-intent teams",
    )

    duel_run = function_body(duel, "void rvDuelGameState::Run( void )")
    queue_update = duel_run.find("UpdateQueue();")
    queue_promotion = duel_run.find("PromoteFromQueue();")
    base_state_run = duel_run.find("rvDMGameState::Run();")
    if not 0 <= queue_update < queue_promotion < base_state_run:
        raise AssertionError(
            "Duel must seed contenders before the base WARMUP state can advance"
        )
    require(
        duel_run,
        "if ( p->wantSpectate )",
        "ordinary Duel spectators remain outside contender seeding",
    )


CANVAS_WIDTH = 640.0
CANVAS_HEIGHT = 480.0
CANVAS_ASPECT = CANVAS_WIDTH / CANVAS_HEIGHT
# Mirrors guis/mphud.gui: crop toward 16:9, floored at the authored 34/480 bar
# and capped so the frame can never reach the stock HUD row at canvas y=57.
LETTERBOX_TARGET_RECIPROCAL = 0.5625
LETTERBOX_MIN_FRACTION = 0.0708333
LETTERBOX_MAX_FRACTION = 0.11
STOCK_HUD_TOP_ROW = 57.0


def match_block(haystack: str, pattern: str, context: str) -> str:
    found = re.search(pattern, haystack, re.DOTALL)
    if found is None:
        raise AssertionError(f"Missing /{pattern}/ in {context}")
    return found.group(0)


def letterbox_metrics(aspect: float, aspect_correction: bool = True) -> dict:
    """Model the authored arena letterbox for one physical viewport aspect."""

    x_expand = y_expand = 0.0
    if aspect_correction:
        if aspect > CANVAS_ASPECT:
            x_expand = CANVAS_WIDTH * (aspect / CANVAS_ASPECT - 1.0) / 2.0
        elif aspect < CANVAS_ASPECT:
            y_expand = CANVAS_HEIGHT * (CANVAS_ASPECT / aspect - 1.0) / 2.0

    screen_width = CANVAS_WIDTH + 2.0 * x_expand
    screen_height = CANVAS_HEIGHT + 2.0 * y_expand

    raw = (1.0 - aspect * LETTERBOX_TARGET_RECIPROCAL) * 0.5
    fraction = (
        LETTERBOX_MIN_FRACTION
        + (raw - LETTERBOX_MIN_FRACTION) * (raw > LETTERBOX_MIN_FRACTION)
        - (raw - LETTERBOX_MAX_FRACTION) * (raw > LETTERBOX_MAX_FRACTION)
    )
    bar = fraction * screen_height
    return {
        "x_expand": x_expand,
        "y_expand": y_expand,
        "screen_width": screen_width,
        "screen_height": screen_height,
        "fraction": fraction,
        "bar": bar,
        "screen_left": -x_expand,
        "screen_right": CANVAS_WIDTH + x_expand,
        "top": -y_expand + bar,
        "bottom": CANVAS_HEIGHT + y_expand - bar,
    }


def validate_letterbox() -> None:
    """The entrance and final-tableau framing must respect the real viewport.

    Authoring the bars as a fixed slice of the 640x480 canvas is only correct at
    4:3: on a wide display the canvas is expanded and the bars stop short of the
    screen edges, and on a tall one the real screen edge is outside the canvas
    entirely, so the world stays visible above and below the frame.
    """

    mp_hud = read(ROOT / "content" / "baseoq4" / "pak0" / "guis" / "mphud.gui")
    user_interface = read(ROOT / "src" / "ui" / "UserInterface.cpp")

    # The engine has to publish the physical aspect; expansion alone cannot
    # describe the viewport when ui_aspectCorrection stretches the canvas.
    require(
        user_interface,
        'SetStateFloat( "virtual_screen_aspect", uiManagerLocal.dc.GetCanvasAspect() );',
        "published viewport aspect for cinematic gui framing",
    )
    require(mp_hud, '"gui::virtual_screen_aspect"', "aspect-driven Arena letterbox")

    for window_name in (
        "oq4_arena_frame_fit",
        "oq4_arena_frame_metrics",
        "oq4_arena_frame",
    ):
        require(mp_hud, f"windowDef {window_name}", "Arena letterbox geometry driver")

    # Every full-bleed layer of the presentation must span the expanded canvas
    # on both axes rather than the authored 640x480 rectangle.
    full_bleed_x = (
        '( 0 - "gui::virtual_screen_x_expand" )',
        '( 640 + ( "gui::virtual_screen_x_expand" * 2 ) )',
    )
    for window_name in (
        "oq4_arena_present_shade",
        "oq4_arena_letterbox_top",
        "oq4_arena_letterbox_bottom",
    ):
        block = match_block(
            mp_hud,
            rf"windowDef\s+{window_name}\s*\{{.*?\n\t\}}",
            f"{window_name} definition",
        )
        for needle in full_bleed_x:
            require(block, needle, f"{window_name} must span the expanded canvas")

    # The bars have to hang off the physical screen edges, and the animation has
    # to run through the shared reveal scalar instead of hard-coded rects.
    require(
        mp_hud,
        '( "oq4_arena_frame::bar" * "oq4_arena_frame::open" )',
        "Arena letterbox height driven by the animated frame",
    )
    require(
        mp_hud,
        '( ( 480 + "gui::virtual_screen_y_expand" ) - ( "oq4_arena_frame::bar" * "oq4_arena_frame::open" ) )',
        "Arena bottom bar anchored to the physical screen edge",
    )
    if re.search(r'transition\s+"oq4_arena_letterbox_(top|bottom)::rect"', mp_hud):
        raise AssertionError(
            "Arena letterbox bars must not transition literal canvas rects"
        )

    # Content that can collide with a thicker bar hangs off the frame edges.
    for needle in (
        '( "oq4_arena_frame::top" + 6 )',
        '( "oq4_arena_frame::bottom" - 7 )',
        '( "oq4_arena_frame::top" + 32 )',
        '( "oq4_arena_frame::top" + 60 )',
        '( "oq4_arena_frame::top" + 119 )',
        '( "oq4_arena_frame::bottom" - 118 )',
        '( "oq4_arena_frame::bottom" - 107 )',
        '( "oq4_arena_frame::bottom" - 84 )',
        '( "oq4_arena_frame::bottom" - 31 )',
    ):
        require(mp_hud, needle, "Arena tableau anchored to the cinematic frame")

    # Numeric contract for the authored policy.
    wide = letterbox_metrics(16.0 / 9.0)
    if abs(wide["bar"] - 34.0) > 0.05 or abs(wide["top"] - 34.0) > 0.05:
        raise AssertionError(
            f"16:9 must reproduce the authored 34-unit bar, got {wide['bar']:.3f}"
        )

    aspects = (32 / 9, 21 / 9, 16 / 9, 1.6, 1.5, 4 / 3, 1.25, 1.0, 0.75, 9 / 16)
    for aspect in aspects:
        for correction in (True, False):
            m = letterbox_metrics(aspect, correction)
            if m["bar"] <= 0.0:
                raise AssertionError(
                    f"aspect {aspect:.3f} would draw no letterbox at all"
                )
            # Bars always reach both horizontal screen edges.
            if m["screen_left"] > 0.0 or m["screen_right"] < CANVAS_WIDTH:
                raise AssertionError(f"aspect {aspect:.3f} leaves an uncovered edge")
            # Bars always sit on the physical top and bottom of the screen.
            if abs((m["top"] - m["bar"]) - (-m["y_expand"])) > 1e-6:
                raise AssertionError(
                    f"aspect {aspect:.3f} top bar is not anchored to the screen edge"
                )
            # And they never swallow the authored 4:3 content.
            if m["top"] > STOCK_HUD_TOP_ROW - 4.0:
                raise AssertionError(
                    f"aspect {aspect:.3f} letterbox reaches the stock HUD row "
                    f"(inner edge {m['top']:.2f})"
                )
            if m["bottom"] < CANVAS_HEIGHT - (STOCK_HUD_TOP_ROW - 4.0):
                raise AssertionError(
                    f"aspect {aspect:.3f} bottom letterbox intrudes too far"
                )

    # Narrower than 16:9 must actually thicken the frame, not stay put.
    if not (
        letterbox_metrics(4 / 3)["fraction"]
        > letterbox_metrics(1.5)["fraction"]
        > letterbox_metrics(16 / 9)["fraction"]
    ):
        raise AssertionError("Arena letterbox must thicken as the display narrows")

    # A "$window::var" transition source resolves once at parse time, so it
    # carries the authored value and silently turns a fade into a snap.
    for gui_path in (ARENA_MENU, ROOT / "content" / "baseoq4" / "pak0" / "guis" / "mphud.gui"):
        text = read(gui_path)
        for match in re.finditer(r'transition\s+"(oq4_arena|arena)[^"]*"\s+"\$', text):
            raise AssertionError(
                f"{gui_path.name}: Arena transition sources a parse-time snapshot "
                f"near offset {match.start()}"
            )


def validate_transition_ownership() -> None:
    """stopTransitions only reaches the calling window's own subtree.

    A transition belongs to the window whose script created it, so naming the
    animated window instead of the timeline that owns the animation is silently
    a no-op and lets a restarted timeline stack duplicate transitions.
    """

    for gui_path in (
        ARENA_MENU,
        ROOT / "content" / "baseoq4" / "pak0" / "guis" / "mphud.gui",
    ):
        text = read(gui_path)
        for name, body in re.findall(
            r"windowDef\s+((?:arena|oq4_arena)_[A-Za-z0-9_]*"
            r"(?:timeline|entrance|victory|clear))\s*\{(.*?)\n\t\}",
            text,
            re.S,
        ):
            for target in re.findall(r'stopTransitions\s+"([^"]+)"', body):
                if target != name:
                    raise AssertionError(
                        f"{gui_path.name}: {name} cannot stop transitions owned by "
                        f"{target}; only the Desktop can reach a sibling"
                    )

    mp_hud = read(ROOT / "content" / "baseoq4" / "pak0" / "guis" / "mphud.gui")
    for event_name, stopped in (
        ("arenaCampaignEntrance", ("oq4_arena_present_victory", "oq4_arena_present_clear")),
        ("arenaCampaignVictory", ("oq4_arena_present_entrance", "oq4_arena_present_clear")),
        (
            "arenaCampaignPresentationClear",
            ("oq4_arena_present_entrance", "oq4_arena_present_victory"),
        ),
    ):
        body = match_block(
            mp_hud,
            rf"onNamedEvent\s+{event_name}\s*\{{.*?\n\t\}}",
            f"{event_name} handler",
        )
        for target in stopped:
            require(
                body,
                f'stopTransitions "{target}"',
                f"{event_name} must retire the presentation it replaces",
            )


def validate_docs() -> None:
    user_doc = read(ROOT / "docs" / "user" / "arena-campaign.md")
    readme = read(ROOT / "README.md")
    bot_doc = read(ROOT / "docs" / "dev" / "mp-bots.md")
    release_completion = read(ROOT / "docs" / "dev" / "release-completion.md")
    release_notes = read(ROOT / "docs" / "dev" / "releases" / "v0.10.000.md")

    for token in (
        "The first three matches in a tier are available together.",
        "Winning all three unlocks that tier's boss match.",
        "Winning the boss match unlocks the next tier.",
        "A draw does not award a win or unlock progress",
        "recommended action receives focus",
        "controller back action",
        "five tiers",
        "twenty",
        "Mission",
        "q4cmp",
        "do not yet pursue flags or map objectives",
        "Its bars always reach every screen edge",
        "The frame lifts\nas control returns.",
        # The ordered ceremony the guide promises, in order.
        "**Introductions.**",
        "**Countdown.**",
        "**Spawn-in.**",
        "**Final tableau.**",
        "**Scoreboard.**",
        "**Match stats.**",
        "no spectator mode and no team picker",
    ):
        require(user_doc, token, "Arena Campaign player guide")
    require(
        readme,
        "[Arena Campaign](https://github.com/themuffinator/openQ4/blob/v0.13.2/docs/user/arena-campaign.md)",
        "Arena Campaign README link",
    )
    for token in (
        "arena/openq4_campaign.cfg",
        "Match indexes 0 through",
        "Only kill-driven modes appear",
    ):
        require(bot_doc, token, "Arena Campaign bot documentation")
    require(
        release_completion,
        "Single-player now includes an Arena Campaign",
        "Arena Campaign candidate changelog",
    )
    for token in (
        "# openQ4 0.10.000",
        "A new Arena Campaign joins single-player.",
        "No extra maps or replacement assets are required.",
        "explicit draw results",
        "The campaign currently focuses on combat-driven modes.",
    ):
        require(release_notes, token, "Arena Campaign release notes")


def validate_failure_reporting() -> None:
    """Every abandoned Arena transaction must name a reason on screen.

    Returning to the browser in silence is indistinguishable from the menu
    ignoring the button, and the only other diagnostic is a console warning
    behind a fullscreen menu.
    """

    arena = read(ROOT / "src" / "framework" / "ArenaCampaign.cpp")
    arena_header = read(ROOT / "src" / "framework" / "ArenaCampaign.h")
    async_network = read(ROOT / "src" / "framework" / "async" / "AsyncNetwork.cpp")
    arena_menu = read(ARENA_MENU)

    for token in (
        "enum arenaFailure_t {",
        "ARENA_FAILURE_NONE = 0,",
        "ARENA_FAILURE_SERVER,",
        "ARENA_FAILURE_BOTS,",
        "ARENA_FAILURE_RESULT,",
        "ARENA_FAILURE_OPPONENTS,",
        "ARENA_FAILURE_ABANDONED",
        "static const char *ArenaFailureMessageKey( int failure )",
        'case ARENA_FAILURE_SERVER:\t\treturn "#str_42150";',
        'case ARENA_FAILURE_BOTS:\t\treturn "#str_42069";',
        'case ARENA_FAILURE_RESULT:\t\treturn "#str_42151";',
        'case ARENA_FAILURE_OPPONENTS:\treturn "#str_42161";',
        'case ARENA_FAILURE_ABANDONED:\treturn "#str_42162";',
        "void idArenaCampaign::ReturnToBrowserAfterStartFailure( int failure )",
        "void idArenaCampaign::ReportStartFailure()",
        "void idArenaCampaign::ReportAbandoned( int token )",
    ):
        require(arena, token, "Arena failure reporting")
    require(
        arena_header, "void\t\t\t\t\tReportStartFailure();", "Arena failure reporting entry point"
    )

    # Leaving a match part way through returns to the Arena browser with a
    # report that names the match and offers it again - not to the title
    # screen in silence. The token must be read before AbortMatch clears it.
    require(arena_header, "void\t\t\t\t\tReportAbandoned( int token );", "abandoned-match report entry point")
    session = read(ROOT / "src" / "framework" / "Session.cpp")
    disconnect = function_body(session, "static void Session_Disconnect_f( const idCmdArgs &args )")
    token_at = disconnect.find("const int arenaToken = arenaCampaign.ActiveMatchToken();")
    abort_at = disconnect.find("arenaCampaign.AbortMatch();")
    report_at = disconnect.find("arenaCampaign.ReportAbandoned( arenaToken );")
    if not 0 <= token_at < abort_at < report_at:
        raise AssertionError("disconnect must report an abandoned Arena match after restoring its transaction")
    abandoned = function_body(arena, "void idArenaCampaign::ReportAbandoned( int token )")
    for token in ("ArenaSetResultMatch( state, token );", "state->resultFailure = ARENA_FAILURE_ABANDONED;", "OpenBrowser();"):
        require(abandoned, token, "the abandoned report keeps the match so it can be retried")
    # Both match-level failures keep their token, so the ledger offers RETRY.
    can_retry = function_body(arena, "static bool ArenaResultCanRetry( const arenaCampaignState_t *state )")
    for token in ("ARENA_FAILURE_OPPONENTS", "ARENA_FAILURE_ABANDONED", "ArenaMatchUnlocked("):
        require(can_retry, token, "Retry follows the same match and the same unlock rule RetryResult applies")
    retry = function_body(arena, "void idArenaCampaign::RetryResult()")
    require(retry, "DismissResult();", "a refused retry must close the already-collapsed report")
    require_regex(
        arena_menu,
        r"windowDef\s+arena_result_retry\s*\{[^}]*?"
        r'\(\s*"gui::arena_result"\s*==\s*2\s*\|\|\s*"gui::arena_result"\s*==\s*3\s*\)',
        "an abandoned or aborted match offers RETRY",
    )

    # A bare bool reintroduces the silent bounce this replaced.
    if "ReturnToBrowserAfterStartFailure( false )" in arena:
        raise AssertionError("Arena start failures must report a reason, not bounce silently")
    if "ReturnToBrowserAfterStartFailure( true )" in arena:
        raise AssertionError("Arena start failures must use a named arenaFailure_t reason")
    # Three PopulateBots launch failures and the spawnServer relay report a
    # server failure, partial bot population reports a bot failure, and
    # FinishPendingCompletion reports a lost result when RecordPendingCompletion
    # discards it (either of its two refusals).
    reasons = re.findall(r"ReturnToBrowserAfterStartFailure\(\s*(ARENA_FAILURE_\w+)\s*\)", arena)
    expected_reasons = {
        "ARENA_FAILURE_SERVER": 4,
        "ARENA_FAILURE_BOTS": 1,
        "ARENA_FAILURE_RESULT": 1,
    }
    actual_reasons = {name: reasons.count(name) for name in set(reasons)}
    if actual_reasons != expected_reasons:
        raise AssertionError(
            f"Arena failure returns are {actual_reasons}, expected {expected_reasons}"
        )

    # SpawnServer fails before any listen server exists, so it cannot go through
    # PopulateBots.  It must still raise the same report.
    require(
        async_network,
        "arenaCampaign.ReportStartFailure();",
        "Arena spawnServer failure reporting",
    )
    if "session->StartMenu( false );\n\t\tarenaCampaign.OpenBrowser();" in async_network:
        raise AssertionError("spawnServer must not return to the Arena browser in silence")

    # A campaign that failed to parse is not a missing map.
    require(arena, 'ArenaLocalized( "#str_42153" )', "Arena campaign-load failure copy")
    require(arena, 'ArenaLocalized( "#str_42152" )', "Arena campaign-load failure title")
    # The unloaded branch has to publish a whole board: the per-slot keys below
    # it are never written on this path, and an unset arena_tier%d_locked reads
    # as 0, drawing every tier and match as unlocked over blank rows.
    unloaded = function_body(arena, "void idArenaCampaign::PublishGui( idUserInterface *gui )")
    unloaded = unloaded.split("if ( !state->loaded ) {", 1)[1].split("\n\t\treturn;", 1)[0]
    for token in (
        'va( "arena_tier%d_locked", tierIndex ), true',
        'va( "arena_match%d_locked", matchIndex ), true',
        'va( "arena_tier%d_name", tierIndex ), ""',
        'va( "arena_match%d_name", matchIndex ), ""',
        'SetStateBool( "arena_canStart", false )',
        'ArenaLocalized( "#str_42153" )',
    ):
        require(unloaded, token, "Arena browser must lock every row when the campaign is unavailable")

    # The overlays own the screen while they are up.  They block by returning a
    # command, not with "modal": idWindow::BringToTop reorders modal windows to
    # the end of the child list, which is the first entry the reverse event walk
    # visits, so a modal shade would jump above its own panel on the first stray
    # click and swallow that panel's buttons from then on.
    for window in ("arena_result_shade", "arena_reset_shade", "arena_transition_blocker"):
        body = gui_window_body(arena_menu, window)
        require(
            body,
            'set "cmd" "arenaBlockInput"',
            f"{window} must absorb clicks so they cannot reach the browser behind it",
        )
        if re.search(r"\b(modal|noevents)\s+1", body):
            raise AssertionError(
                f"{window} must stop events by returning a command, not with modal/noevents"
            )
    require(
        read(ROOT / "src" / "framework" / "ArenaCampaign.cpp"),
        'if ( !idStr::Icmp( cmd, "arenaBlockInput" ) ) {',
        "Arena overlay input blocker must be a handled command",
    )

    # arena_campaignComplete tracks live progress, so a later replay would show
    # the banner - and lose its awards - without gating it on the win that
    # actually completed the campaign.
    require_regex(
        arena_menu,
        r"windowDef\s+arena_result_campaign_complete\s*\{[^}]*?"
        r'"gui::arena_resultUnlockedCampaign"\s*==\s*1\s*&&\s*"gui::arena_result"\s*==\s*1',
        "campaign-complete banner must be gated on the completing victory",
    )
    for window in ("arena_result_awards_label", "arena_result_awards"):
        require(
            gui_window_body(arena_menu, window),
            '"gui::arena_resultUnlockedCampaign" == 0',
            f"{window} must show on every win except the one that completed the campaign",
        )
    require(gui_window_body(arena_menu, "arena_result_awards"), "wrap\t\t1",
            "a long award list must wrap, not be cut off mid-word")

    # The engine sends a pending timeline command in the same string as a
    # clicked window's command, so the blocker must not end the string, and
    # controls behind an open overlay - reachable by keyboard and controller
    # focus - must not run.
    gui_command = function_body(arena, "bool idArenaCampaign::HandleGuiCommand( const char *menuCommand )")
    blocker = gui_command[gui_command.find('if ( !idStr::Icmp( cmd, "arenaBlockInput" ) ) {'):]
    blocker = blocker[: blocker.find("}") + 1]
    if "return" in blocker or "continue;" not in blocker:
        raise AssertionError("arenaBlockInput must mark the event taken and keep reading the command string")
    for token in ("const bool overlayCommand =", "( resultOpen || resetOpen ) && !overlayCommand"):
        require(gui_command, token, "Arena overlays must refuse the controls beneath them")
    open_browser = function_body(arena, "void idArenaCampaign::OpenBrowser()")
    require(open_browser, "if ( state->result != 0 ) {", "a reopened browser must reveal any open report again")

    # The real seat ceiling is game_mp's si_maxPlayers range, not MAX_ASYNC_CLIENTS.
    # A full engine reload replays a bare spawnServer after session shutdown has
    # already aborted the campaign transaction, landing the player in an ordinary
    # multiplayer match on the campaign map.
    require(
        arena,
        'cvarSystem->SetCVarInteger( "net_serverReloadEngine", 0 );',
        "Arena launch must suppress the full engine reload",
    )
    require(
        arena,
        '\t"net_serverReloadEngine",\n',
        "Arena must hand net_serverReloadEngine back after the match",
    )

    require(arena, "static const int ARENA_MAX_SEATS = 16;", "Arena seat ceiling")
    require(arena, "match.bots.Num() + 1 > ARENA_MAX_SEATS", "Arena seat ceiling validation")
    code = re.sub(r"//[^\n]*|/\*.*?\*/", "", arena, flags=re.DOTALL)
    if "MAX_ASYNC_CLIENTS" in code:
        raise AssertionError(
            "Arena roster size must be validated against the si_maxPlayers ceiling"
        )


def validate_result_contract() -> None:
    """Pin the numeric result contract that crosses the engine/game boundary.

    game_mp sends `arenaComplete <token> <outcome> <player> <opponent>` and the
    framework parses the outcome by value, so reordering either enum silently
    turns draws into wins.
    """

    arena = read(ROOT / "src" / "framework" / "ArenaCampaign.cpp")
    game = read(GAME_LIBS_ROOT / "src" / "mpgame" / "MultiplayerGame.cpp")

    engine_outcomes = dict(
        re.findall(r"(ARENA_OUTCOME_\w+)\s*=\s*(\d+)", arena)
    )
    game_outcomes = dict(
        re.findall(r"(ARENA_RESULT_\w+)\s*=\s*(\d+)", game)
    )
    expected = {"LOSS": "0", "WIN": "1", "DRAW": "2"}
    for suffix, value in expected.items():
        if engine_outcomes.get(f"ARENA_OUTCOME_{suffix}") != value:
            raise AssertionError(
                f"ARENA_OUTCOME_{suffix} must be {value}, found "
                f"{engine_outcomes.get(f'ARENA_OUTCOME_{suffix}')}"
            )
        if game_outcomes.get(f"ARENA_RESULT_{suffix}") != value:
            raise AssertionError(
                f"ARENA_RESULT_{suffix} must be {value}, found "
                f"{game_outcomes.get(f'ARENA_RESULT_{suffix}')}"
            )

    require(
        game,
        'gameLocal.sessionCommand = va( "arenaComplete %d %d %d %d %d",',
        "Arena result command shape",
    )
    require(
        arena,
        'const bool arity = args.Argc() == 5 || args.Argc() == 6;',
        "Arena result command arity",
    )
    # Awards ride a single bounded bit mask so they fit one command argument and
    # one ArenaParseBoundedInteger range. The framework must accept the wider
    # arity before any game build starts sending it.
    require(arena, "static const int ARENA_AWARD_COUNT = 11;", "Arena award mask width")
    require(
        arena,
        "static const int ARENA_AWARD_MASK = ( 1 << ARENA_AWARD_COUNT ) - 1;",
        "Arena award mask",
    )
    stat_header = read(
        GAME_LIBS_ROOT / "src" / "mpgame" / "mp" / "stats" / "StatManager.h"
    )
    stat_manager_source = read(
        GAME_LIBS_ROOT / "src" / "mpgame" / "mp" / "stats" / "StatManager.cpp"
    )
    award_enum = re.search(r"enum endGameAward_t \{(.*?)\}", stat_header, re.DOTALL)
    if award_enum is None:
        raise AssertionError("Missing endGameAward_t in game_mp")
    award_names = re.findall(r"\bEGA_\w+", award_enum.group(1))
    # EGA_INVALID .. EGA_PERFECT are the ids that ride the mask; EGA_NUM_AWARDS is
    # the sentinel. ARENA_AWARD_COUNT must equal the sentinel's value, so the mask
    # width has to move whenever an award is added.
    if (
        award_names[0] != "EGA_INVALID"
        or award_names[-1] != "EGA_NUM_AWARDS"
        or len(award_names) - 1 != 11
    ):
        raise AssertionError(
            f"endGameAward_t shape changed ({award_names}); "
            "ARENA_AWARD_COUNT in ArenaCampaign.cpp must follow"
        )
    # rvStatManager::EndGame computes the awards on the GAMEREVIEW enter edge,
    # which is AFTER BeginArenaCampaignResult queues the result but BEFORE the
    # tableau elapses and the command is written. Packing them anywhere but at
    # emission time silently ships an empty mask.
    require(game, "static int ArenaCampaignAwardMask( int clientNum )", "Arena award packing")
    require(game, "mask |= 1 << award;", "Arena award mask packing")
    report_body = function_body(
        game, "void idMultiplayerGame::UpdateArenaCampaignResult( void )"
    )
    require(report_body, "ArenaCampaignAwardMask(", "awards must be packed when the result is emitted")
    begin_body = function_body(
        game, "void idMultiplayerGame::BeginArenaCampaignResult( void )"
    )
    if "ArenaCampaignAwardMask" in begin_body:
        raise AssertionError(
            "awards are not computed yet when the result is queued; pack them at emission"
        )
    require(
        read(ROOT / "src" / "framework" / "ArenaCampaign.cpp"),
        'gui->SetStateString( "arena_resultAwardLine", awardLine );',
        "Arena award line must reach the result ledger",
    )
    require(read(ARENA_MENU), '"gui::arena_resultAwardLine"', "result ledger must render awards")

    # The framework renders award names from the stock contiguous string block.
    require(
        arena,
        "static const int ARENA_AWARD_FIRST_STRING = 107259;",
        "Arena award name base",
    )
    for award_index, award in enumerate(award_names[1:-1], start=1):
        expected = '"#str_%d"' % (107259 + award_index)
        if expected not in stat_manager_source:
            raise AssertionError(
                f"{award} no longer uses {expected}; the framework award name base is wrong"
            )

    # game_mp derives the entrance title from the manifest's #str numbering, so
    # the stride lives in two repositories at once: validate_manifest pins the
    # .cfg to 42100 + index * 2, and this pins game_mp to the same arithmetic.
    require(
        game,
        "static const int ARENA_MATCH_TITLE_FIRST_STRING = 42100;",
        "Arena match title base shared with the campaign manifest",
    )
    require(
        game,
        "ARENA_MATCH_TITLE_FIRST_STRING + Max( 0, token - 1 ) * 2",
        "Arena match title stride shared with the campaign manifest",
    )

    # The final tableau must resolve a collision-safe camera exactly like the
    # entrance; without it a blocked orbit angle hard-cuts to first person and
    # pops the depth of field off mid-ceremony.
    presentation = function_body(
        game,
        "bool idMultiplayerGame::BuildArenaCampaignPresentationView( idPlayer *viewer, renderView_t *view )",
    )
    for token in (
        "const bool cameraLatched =",
        "arenaEntranceCameraIsEntrance == entrance",
        "reviewProbeAngles",
        "if ( !cameraLatched ) {",
        "arenaEntranceCameraIsEntrance = entrance;",
    ):
        require(presentation, token, "Arena tableau camera resolution")
    for token in (
        "if ( entrance && !arenaEntranceCameraResolved )",
        "if ( entrance && arenaEntranceCameraFallback )",
        "if ( entrance && !arenaEntranceCameraValid )",
    ):
        if token in presentation:
            raise AssertionError(
                f"Arena camera resolution must cover the final tableau too: {token!r}"
            )

    # Single player: no spectating, no side switching. si_spectators false is the
    # authoritative server-side block (idPlayer::UserInfoChanged forces ui_spectate
    # back to "Play"); the menu entry points refuse an arena request on top of it.
    require(
        read(ROOT / "src" / "framework" / "ArenaCampaign.cpp"),
        'cvarSystem->SetCVarBool( "si_spectators", false );',
        "Arena must disable spectators",
    )
    for entry in (
        "void idMultiplayerGame::JoinTeam( const char* team )",
        "void idMultiplayerGame::ToggleSpectate( void )",
        "void idMultiplayerGame::ToggleTeam( void )",
    ):
        body = function_body(game, entry)
        require(body, "if ( IsArenaCampaignMatch() ) {", f"{entry} must refuse an Arena request")

    # The final tableau is steered by the player, so look input has to keep
    # arriving there while the bodies stay frozen where the match ended.
    free_look = function_body(
        game, "bool idMultiplayerGame::ArenaCampaignAllowsFreeLook( void ) const"
    )
    require(free_look, "GAMEREVIEW", "free look belongs to the final tableau only")
    require(free_look, "ArenaCampaignLocksPlayers()", "free look is a locked-phase concession")
    player_source = read(GAME_LIBS_ROOT / "src" / "mpgame" / "Player.cpp")
    update_view = function_body(player_source, "void idPlayer::UpdateViewAngles( void )")
    require(update_view, "ArenaCampaignAllowsFreeLook()", "Arena free look must reach view angles")
    require(
        update_view,
        "if ( !arenaFreeLook ) {\n\t\t// orient the model towards the direction we're looking\n\t\tSetAngles(",
        "Arena free look must not turn the frozen player model",
    )

    # si_arenaCampaign is replicated serverinfo; the ceremony is listen-server only.
    is_arena_match = function_body(
        game, "bool idMultiplayerGame::IsArenaCampaignMatch( void ) const"
    )
    for token in (
        "gameLocal.isListenServer",
        "gameLocal.isClient",
        "gameLocal.isServer",
    ):
        require(is_arena_match, token, "Arena ceremony must be confined to the local listen server")


def validate_best_results() -> None:
    """Pin the per-match best result: its persistence, repair, ordering and UI.

    ui_arenaBest sits beside ui_arenaProgress and must agree with it, so it is
    repaired with it, cleared with it, and only a contested result can change
    it. The ordering model below mirrors ArenaBestResultBeats.
    """

    arena = read(ROOT / "src" / "framework" / "ArenaCampaign.cpp")
    arena_menu = read(ARENA_MENU)

    for token in (
        '"ui_arenaBest", ""',
        "CVAR_SYSTEM | CVAR_ARCHIVE,",
        "struct arenaBestResult_t",
        "static bool ArenaBestResultValid",
        "static bool ArenaBestResultBeats",
        "static arenaBestResult_t ArenaEffectiveBest",
        "static int ArenaReadBestResults",
        "static idStr ArenaWriteBestResults",
        "static void ArenaRepairPersistedBest()",
        "static const int outcomeRank[] = { 0, 2, 1 };",
    ):
        require(arena, token, "Arena best-result record")

    repair_progress = function_body(arena, "static void ArenaRepairPersistedProgress()")
    require(
        repair_progress,
        "ArenaRepairPersistedBest();",
        "best results are repaired against the repaired progress",
    )
    read_body = function_body(
        arena,
        "static int ArenaReadBestResults( const char *text, int progress, "
        "arenaBestResult_t results[ARENA_TOTAL_MATCHES] )",
    )
    for token in (
        "fieldCount != 5",
        "ArenaParseBoundedInteger( fields[0].c_str(), 0, ARENA_TOTAL_MATCHES - 1, index )",
        "ARENA_OUTCOME_LOSS, ARENA_OUTCOME_DRAW, result.outcome",
        "ArenaParseBoundedInteger( fields[4].c_str(), 1, 5, result.difficulty )",
        "!ArenaBestResultValid( result ) || results[index].recorded",
        "!ArenaMatchUnlocked( progress, tier, match )",
        "( result.outcome == ARENA_OUTCOME_WIN ) != ArenaMatchComplete( progress, tier, match )",
    ):
        require(read_body, token, "strict Arena best-result parsing")
    if "atoi(" in read_body:
        raise AssertionError("Arena best results must not accept partial integers")

    load_body = function_body(arena, "bool idArenaCampaign::LoadCampaign()")
    require(load_body, 'ui_arenaBest.SetString( "" );', "a new campaign version clears best results")
    reset_body = function_body(arena, "void idArenaCampaign::ResetProgress()")
    require(reset_body, 'ui_arenaBest.SetString( "" );', "Reset Progress clears best results")
    for signature in (
        "void idArenaCampaign::RequestResetProgress()",
        "void idArenaCampaign::ResetProgress()",
    ):
        require(
            function_body(arena, signature),
            "ui_arenaProgress.GetInteger() == 0 && !ArenaHasBestResults()",
            "best results alone make Reset Progress available",
        )

    finish_body = function_body(arena, "bool idArenaCampaign::RecordPendingCompletion()")
    for token in (
        "if ( state->pendingOutcome != ARENA_OUTCOME_ABORTED ) {",
        "ArenaReadBestResults( ui_arenaBest.GetString(), previousProgress, bestResults );",
        "ArenaEffectiveBest( bestResults[matchIndex], previousProgress, tier, match )",
        "ArenaBestResultBeats( candidate, record )",
        "newBest = bestResults[matchIndex].recorded;",
        "state->resultNewBest = newBest;",
        "state->matchDifficulty",
    ):
        require(finish_body, token, "Arena best result updated from a contested result")
    if finish_body.index("ArenaReadBestResults(") > finish_body.index("progress |= ArenaMatchBit"):
        raise AssertionError("the best result must be read against the progress before this win")
    require(
        function_body(arena, "void idArenaCampaign::PopulateBots()"),
        "state->matchDifficulty = difficulty;",
        "the best result keeps the difficulty the match was played on",
    )
    abandoned = function_body(arena, "void idArenaCampaign::ReportAbandoned( int token )")
    if "ui_arenaBest" in abandoned:
        raise AssertionError("an abandoned match must not touch the best results")

    # PublishGui publishes to the stock browser and the retained page alike.
    update_body = function_body(arena, "void idArenaCampaign::PublishGui( idUserInterface *gui )")
    for token in (
        '"arena_selectedBestVisible"',
        '"arena_selectedHasBest"',
        '"arena_selectedBest"',
        '"#str_42166"',
        '"arena_canReset", progress != 0 || ArenaHasBestResults()',
        '"arena_resultNewBest", state->result != 0 && state->resultNewBest',
    ):
        require(update_body, token, "Arena best-result GUI state")

    for window, tokens in (
        ("arena_best_label", ('"#str_42040"', "arena_selectedBestVisible")),
        ("arena_best_value", ('"gui::arena_selectedBest"', 'arena_selectedHasBest" == 1')),
        ("arena_best_none", ('"gui::arena_selectedBest"', 'arena_selectedHasBest" == 0')),
        ("arena_result_new_best", ('"#str_42165"', "arena_resultNewBest")),
        ("arena_browser_reset", ('arena_canReset" == 1',)),
    ):
        body = gui_window_body(arena_menu, window)
        for token in tokens:
            require(body, token, f"Arena browser {window}")

    # parse_language holds the CP1252 tables to their font set; here every
    # table, whatever its script, only has to carry the strings.
    tables = sorted(STRINGS_DIR.glob("*_openq4.lang"))
    if len(tables) != 12:
        raise AssertionError(f"expected 12 openQ4 language tables, found {len(tables)}")
    for path in tables:
        text = path.read_text(encoding="utf-8-sig")
        for key in ("#str_42040", "#str_42165", "#str_42166"):
            if not re.search(rf'"{key}"\s+"[^"\s][^"]*"', text):
                raise AssertionError(f"{path.name} lacks Arena best-result string {key}")

    # Ordering model: win > draw > loss, then margin, then difficulty, then the
    # higher score; a scoreless record loses to a scored one of its outcome.
    rank = {0: 0, 1: 2, 2: 1}

    def beats(candidate: tuple, record: tuple | None) -> bool:
        if record is None:
            return True
        c_outcome, c_player, c_opponent, c_difficulty = candidate
        r_outcome, r_player, r_opponent, r_difficulty = record
        if rank[c_outcome] != rank[r_outcome]:
            return rank[c_outcome] > rank[r_outcome]
        c_scored = c_player is not None
        if c_scored != (r_player is not None):
            return c_scored
        if c_scored and c_player - c_opponent != r_player - r_opponent:
            return c_player - c_opponent > r_player - r_opponent
        if c_difficulty != r_difficulty:
            return c_difficulty > r_difficulty
        return c_scored and c_player > r_player

    ordering_cases = (
        ((1, 10, 3, 3), None, True),
        ((2, 5, 5, 3), (0, 1, 10, 5), True),
        ((0, 9, 10, 3), (2, 5, 5, 3), False),
        ((1, 10, 9, 5), (1, 10, 3, 1), False),
        ((1, 10, 2, 1), (1, 10, 3, 5), True),
        ((1, 10, 3, 4), (1, 10, 3, 3), True),
        ((1, 12, 5, 3), (1, 10, 3, 3), True),
        ((1, 10, 3, 3), (1, 10, 3, 3), False),
        # A match won before best results were kept: a scoreless win.
        ((1, 10, 9, 1), (1, None, None, 0), True),
        ((2, 5, 5, 5), (1, None, None, 0), False),
    )
    for candidate, record, expected in ordering_cases:
        if beats(candidate, record) != expected:
            raise AssertionError(
                f"Arena best-result ordering drifted: {candidate} vs {record}"
            )


def validate_ceremony_flow() -> None:
    """Pin the ordered SP Arena match ceremony.

    The flow is: introduce the opponents in warmup, count down, hold control for
    a match-start camera move that lands in first person, play the match, freeze
    the world and let the player orbit the victor, then scoreboard, then match
    stats, then hand off to the framework.
    """

    game = read(GAME_LIBS_ROOT / "src" / "mpgame" / "MultiplayerGame.cpp")
    header = read(GAME_LIBS_ROOT / "src" / "mpgame" / "MultiplayerGame.h")
    state = read(GAME_LIBS_ROOT / "src" / "mpgame" / "mp" / "GameState.cpp")
    game_local = read(GAME_LIBS_ROOT / "src" / "mpgame" / "Game_local.cpp")

    # The ceremony is host-local on purpose. An Arena match is one human on a
    # listen server with no remote clients, so adding mpGameState_t values (wire
    # bytes every gametype branches on) or an mpMatchSession pause kind would be
    # far more invasive than the flow needs.
    for phase in (
        "ARENA_CEREMONY_NONE = 0,",
        "ARENA_CEREMONY_INTRO,",
        "ARENA_CEREMONY_SPAWN_IN,",
        "ARENA_CEREMONY_TABLEAU,",
        "ARENA_CEREMONY_SCOREBOARD,",
        "ARENA_CEREMONY_STATS,",
        "ARENA_CEREMONY_DONE",
    ):
        require(header, phase, "Arena ceremony phases")
    match_phase = read(GAME_LIBS_ROOT / "src" / "mpgame" / "mp" / "MatchPhase.h")
    if "ARENA" in match_phase:
        raise AssertionError(
            "the Arena ceremony must stay host-local; mpGameState_t values are replicated wire bytes"
        )

    # No gametype may bench the human. game_mp's Duel is a queue-based tournament
    # mode that holds every non-contender in spectator, so the campaign runs its
    # one-on-one matches as plain deathmatch and only presents them as Duels.
    arena = read(ROOT / "src" / "framework" / "ArenaCampaign.cpp")
    require(arena, "static const char *ArenaEffectiveGameType(", "Duel must run as deathmatch")
    effective = function_body(arena, "static const char *ArenaEffectiveGameType( const idStr &gameType )")
    require(effective, 'gameType.Icmp( "Duel" ) == 0', "Duel must be remapped")
    require(effective, 'return "DM";', "Duel must run as deathmatch")
    require(
        arena,
        'cvarSystem->SetCVarString( "si_gameType", ArenaEffectiveGameType( match->gameType ) );',
        "the server must run the effective gametype",
    )
    # The browser still advertises Duel, so the authored type must survive.
    require(arena, 'gameType.Icmp( "Duel" ) == 0 ) {\n\t\treturn "#str_42201";', "Duel keeps its label")

    # The competitive managed-match layer must never own a campaign match. Its
    # join evaluator takes over participation, and idPlayer::UserInfoChanged's
    # managed arm only sets forceRespawn when the requested intent CHANGES - the
    # campaign launches with ui_spectate already "Play", so it never does, and
    # the player sits in spectator all match while the bots fight.
    managed = function_body(game, "bool idMultiplayerGame::IsManagedMatch( void ) const")
    require(managed, "if ( IsArenaCampaignMatch() ) {", "the campaign is never a managed match")
    require(
        game,
        'const bool managedMatch = !IsArenaCampaignMatch() && rules.GetBool( MP_RULE_MANAGED_MATCH );',
        "si_managedMatch must agree with IsManagedMatch for the campaign",
    )
    require(game, 'gameLocal.serverInfo.SetBool( "si_managedMatch", managedMatch );',
            "the campaign-aware managed flag must reach server info")
    # A declared-seat roster has nobody to declare it in single player, so the
    # arena arm must clear it after the profile has been applied - not merely
    # rely on the default it was initialised with.
    require_regex(
        game,
        r"if \( IsArenaCampaignMatch\(\) \) \{[^}]*?policy\.requireDeclaredRosterSeats = false;"
        r"[^}]*?rosterSize = 0;",
        "the campaign must clear the declared-seat roster requirement",
    )

    # Belt and braces: several independent layers can bench the human and each
    # fails silently. Put them back rather than trusting every guard.
    require(
        game,
        "if ( IsArenaCampaignMatch() && p->spectating && !p->wantSpectate &&",
        "the campaign player must be restored to play if anything benches them",
    )

    # Sudden death benches everyone who is not the frag leader. In a campaign
    # match that hands the win to a bot and leaves the human watching.
    # Asserted against the whole file rather than the function body: CheckRespawns
    # contains a commented-out `{` that a brace matcher cannot see past.
    require(
        game,
        "if ( gameLocal.IsTeamGame() || p->IsLeader() || IsArenaCampaignMatch() ) {",
        "sudden death must not bench the campaign player",
    )
    require_regex(
        state,
        r"static_cast<\s*idPlayer\s*\*>\(\s*ent\s*\)->IsLeader\(\)\s*\|\|\s*"
        r"gameLocal\.mpGame\.IsArenaCampaignMatch\(\)",
        "the sudden-death edge must not bench campaign players",
    )

    # Round modes announce their own Fight when the round goes active.
    ceremony_fight = function_body(game, "void idMultiplayerGame::AdvanceArenaCampaignCeremony( void )")
    require(
        ceremony_fight,
        "!gameLocal.IsRoundGameType()",
        "spawn-in must not double up the round modes' own Fight",
    )

    player_source = read(GAME_LIBS_ROOT / "src" / "mpgame" / "Player.cpp")

    # Impulses drive reload and weapon selection in multiplayer; default.cfg
    # binds them. The stale Raven assert fired on every one of those.
    if "assert( false ); // unused in multiplayer?" in player_source:
        raise AssertionError("EvaluateControls must not assert on multiplayer impulses")

    # PM_FREEZE returns from MovePlayer before gravity and the ground trace, so
    # a player frozen mid-fall hangs in the air in a falling pose - which is
    # what the match-start shot circles.
    require(
        player_source,
        "physicsObj.SetMovementType( physicsObj.OnGround() ? PM_FREEZE : PM_NORMAL );",
        "an Arena-locked player must land before being frozen",
    )
    require(
        player_source,
        "physicsObj.SetPlayerInput( held, viewAngles );",
        "an Arena-locked player must be input-neutralised, not physics-frozen mid-air",
    )

    # Warmup arms the player with the map's arsenal. Nothing clears
    # inventory.weapons on an MP respawn, so it has to be taken back explicitly
    # or it lasts the whole match.
    require(game, "int idMultiplayerGame::GetMapWeaponMask( void )", "map-limited warmup arsenal")
    require(game, 'ent->spawnArgs.GetString( "inv_weapon", "" )', "warmup arsenal comes from map pickups")
    require(
        player_source,
        "inventory.weapons |= gameLocal.mpGame.GetMapWeaponMask();",
        "warmup must grant only the weapons the map contains",
    )
    if 'GiveStuffToPlayer( this, "weapons", "" );\n\t\t\tGiveStuffToPlayer' in player_source:
        raise AssertionError("warmup must not grant every weapon in the game")
    require(player_source, "void idPlayer::RevokeWarmupArsenal( void )", "warmup arsenal must be revocable")
    require(state, "p->RevokeWarmupArsenal();", "the warmup arsenal must be taken back when the match starts")

    # The ceremony must arm on the same predicate everything else uses. The raw
    # serverinfo token is replicated and archivable, so a server merely carrying
    # it would stall its map rotation and emit a stray arenaComplete.
    require(
        game,
        "if ( !IsArenaCampaignMatch() || arenaResultPending || arenaResultReported ) {",
        "the result ceremony must arm on IsArenaCampaignMatch, not the raw token",
    )

    round_modes = read(GAME_LIBS_ROOT / "src" / "mpgame" / "mp" / "RoundModes.cpp")
    round_state = read(GAME_LIBS_ROOT / "src" / "mpgame" / "mp" / "RoundGameState.cpp")
    mp_hud = read(ROOT / "content" / "baseoq4" / "pak0" / "guis" / "mphud.gui")

    # Freeze Tag: an unattended thaw is the timer giving up, usually because the
    # body is somewhere lethal. Respawning onto it loops the player to death.
    require(
        round_modes,
        "if ( thawer != NULL && FindThawSpot( frozen, thawer, origin ) ) {",
        "an unattended thaw must take a spawn point, not the body",
    )
    # Red Rover converts the victim's side before the warning is sent.
    require(round_modes, "const int oldTeam = dead->team;", "the last-on-side warning must use the side left")
    require(round_modes, "NotifyLastOnSide( oldTeam );", "the last-on-side warning must use the side left")
    # Non-elimination round modes have nobody to eliminate.
    require(
        round_state,
        "player->wantSpectate &&\n\t\t IsEliminationMode() ) {",
        "only elimination modes may mark a spectate toggle as eliminated",
    )
    # Fixed-loadout modes must not leave scavengeable pickups or stale magazines.
    require(
        player_source,
        "memset( inventory.clip, -1, sizeof( inventory.clip ) );",
        "a fixed-loadout respawn must reset magazines",
    )
    require_regex(
        player_source,
        r"MPGameTypeHasAny\( gameLocal\.gameType, GTF_FULLARSENAL \) \) \{\s*return;",
        "fixed-loadout modes must not drop weapons",
    )
    # Red Rover is a two-sided mode and needs the team panel, not the FFA one.
    require(
        mp_hud,
        '"gui::gametype" == 11 || "gui::gametype" == 12 ) {',
        "Red Rover must use the two-team HUD panel",
    )
    if mp_hud.count("rect\t170,58,300,20") > 1:
        raise AssertionError("the overtime banner and the round clock must not share a rect")

    # Warmup introduction. Arena warmup otherwise has zero dwell: the roster is
    # populated before the first game frame and AllPlayersReady short-circuits.
    require(
        state,
        "if ( gameLocal.mpGame.ArenaCampaignIntroBlocksCountdown() ) {",
        "the introduction must hold warmup open",
    )
    require(game, "ShowArenaCampaignIntroCard", "opponents must be introduced by name")
    lock = function_body(game, "bool idMultiplayerGame::ArenaCampaignLocksPlayers( void ) const")
    for phase in ("ARENA_CEREMONY_SPAWN_IN", "ARENA_CEREMONY_INTRO"):
        require(lock, phase, "introduction and spawn-in must lock the combatants")

    # Match start: the shot has to converge on the real first-person view, or the
    # hand-off back to control is a visible cut.
    spawn_in = function_body(
        game,
        "bool idMultiplayerGame::BuildArenaCampaignSpawnInView( idPlayer *viewer, renderView_t *view )",
    )
    for token in ("firstPersonViewOrigin", "firstPersonViewAxis", "ARENA_SPAWN_IN_BLEND_START"):
        require(spawn_in, token, "spawn-in must land exactly on the first-person view")

    # "Fight" belongs with control, not with the camera move.
    if "!gameLocal.mpGame.IsArenaCampaignMatch()" not in state:
        raise AssertionError("the Arena campaign must not shout Fight over its spawn-in shot")

    # Freeze everything, not just the combatants.
    freeze = function_body(game, "bool idMultiplayerGame::ArenaCampaignFreezesWorld( void ) const")
    for phase in ("ARENA_CEREMONY_TABLEAU", "ARENA_CEREMONY_SCOREBOARD", "ARENA_CEREMONY_STATS"):
        require(freeze, phase, "the world must stay frozen for the whole review")
    if "ARENA_CEREMONY_SPAWN_IN" in freeze:
        raise AssertionError("spawn-in holds control but must not stop the world")
    require(
        game_local,
        "arenaCeremonyFrozen = mpGame.ArenaCampaignFreezesWorld();",
        "the Arena freeze must reach the entity think loop",
    )
    # No entity is exempt from the paused think, not even the viewer.
    # idPlayer::ThinkMatchPaused already consumes the usercmd, zeroes movement and
    # impulses and calls UpdateViewAngles(), which is where the tableau's
    # free-look orbit is honoured. Exempting the viewer left their own body
    # animating, their powerups expiring and their corpse dissolving inside a
    # world that had visibly stopped.
    if "ent == player ) {\n\t\t\t\t\tent->Think();" in game_local:
        raise AssertionError(
            "the frozen ceremony must not exempt the viewer from ThinkMatchPaused"
        )
    paused_think = function_body(player_source, "void idPlayer::ThinkMatchPaused( int deltaMsec )")
    for token in ("usercmd.impulse = 0;", "UpdateViewAngles();"):
        require(
            paused_think,
            token,
            "ThinkMatchPaused must neutralise input and still integrate look",
        )

    # Ordered scoreboard then stats, drawn without ever spending the session
    # command the Arena handoff owns.
    ceremony = function_body(game, "void idMultiplayerGame::AdvanceArenaCampaignCeremony( void )")
    for token in ("ForceScoreboard( true, 0 )", "SetupArenaCampaignStatSummary();", "currentMenu = 3;"):
        require(ceremony, token, "the ceremony must play the scoreboard then the stats")
    if "sessionCommand" in ceremony:
        raise AssertionError(
            "the ceremony must not touch gameLocal.sessionCommand; the arenaComplete handoff owns it"
        )
    stat_setup = function_body(game, "void idMultiplayerGame::SetupArenaCampaignStatSummary( void )")
    if "sessionCommand" in stat_setup:
        raise AssertionError("the stat summary must be populated without a menu session command")

    # The fade has to draw over the scoreboard and the summary. idPlayerView's
    # own fade runs inside RenderPlayerView, which is before both.
    require(game, "void idMultiplayerGame::DrawArenaCampaignCeremonyFade( void )", "ceremony fade")
    draw = function_body(game, "bool idMultiplayerGame::Draw( int clientNum )")
    fade_at = draw.find("DrawArenaCampaignCeremonyFade();")
    if fade_at < 0:
        raise AssertionError("the ceremony fade must be drawn from Draw")
    for earlier in ("DrawScoreBoard( player );", "DrawStatSummary();"):
        if draw.find(earlier) > fade_at:
            raise AssertionError(
                f"the ceremony fade must draw after {earlier!r} or it cannot fade between screens"
            )


def validate_ceremony_presentation() -> None:
    """Pin the 2026-10 Arena audit fixes, each of which shipped broken unnoticed.

    The ceremony is host-local and only runs in a real match, so these static
    pins are the guard between runtime checks (openq4_arenaCeremony drives a
    match end to end without device input).
    """

    game = read(GAME_LIBS_ROOT / "src" / "mpgame" / "MultiplayerGame.cpp")
    header = read(GAME_LIBS_ROOT / "src" / "mpgame" / "MultiplayerGame.h")
    state = read(GAME_LIBS_ROOT / "src" / "mpgame" / "mp" / "GameState.cpp")
    game_local = read(GAME_LIBS_ROOT / "src" / "mpgame" / "Game_local.cpp")
    player = read(GAME_LIBS_ROOT / "src" / "mpgame" / "Player.cpp")
    sys_cmds = read(GAME_LIBS_ROOT / "src" / "mpgame" / "gamesys" / "SysCmds.cpp")
    arena = read(ROOT / "src" / "framework" / "ArenaCampaign.cpp")
    mp_hud = read(ROOT / "content" / "baseoq4" / "pak0" / "guis" / "mphud.gui")
    summary = read(ROOT / "content" / "baseoq4" / "pak0" / "guis" / "summary.gui")
    remastered = read(ROOT / "tools" / "ui" / "remastered_hud_mp.py")

    # GameStateChanged runs after Run in the same listen-server frame. Clearing
    # the presentation there wiped the spawn-in OnMatchStarted had just armed
    # (no camera move, no held control, no Fight) and the introduction the
    # WARMUP case had just armed. The reset belongs to the authoritative edge.
    changed = function_body(state, "void rvGameState::GameStateChanged( void )")
    if "ClearArenaCampaignPresentation" in changed:
        raise AssertionError("GameStateChanged must not clear the Arena presentation it runs after")
    new_state = function_body(state, "bool rvGameState::NewState( mpGameState_t newState )")
    warmup = new_state[new_state.find("case WARMUP: {"): new_state.find("case COUNTDOWN: {")]
    require(warmup, "gameLocal.mpGame.ClearArenaCampaignPresentation();", "the Arena warmup edge resets the presentation")
    # The count runs over the spawn-in so Fight lands with control.
    require(changed, "} else if ( !gameLocal.IsRoundGameType() ) {", "the Arena countdown only calls prepare-to-fight")
    started = function_body(game, "void idMultiplayerGame::OnMatchStarted( void )")
    for token in ("AS_GENERAL_THREE, arenaCeremonyPhaseEndTime - 3000", "AS_GENERAL_ONE, arenaCeremonyPhaseEndTime - 1000"):
        require(started, token, "the Arena count runs over the spawn-in shot")

    # 0 degrees is in front of the subject: the authored orbits assume it.
    require(game, "const idVec3 desiredView = focusPoint + radial * range + up * height;",
            "Arena cameras place the eye along radial, so authored angles face the subject")
    intro_view = function_body(game, "bool idMultiplayerGame::BuildArenaCampaignIntroView( idPlayer *viewer, renderView_t *view )")
    for token in ("arenaIntroShotLatched", "pullInFractions"):
        require(intro_view, token, "an introduction picks a clear side and pulls in rather than cutting away")

    # Introductions are their own HUD mode, ordered teammates then opponents.
    require(game, '"arenaCampaignIntroduce"', "introductions use their own presentation mode")
    order = function_body(game, "void idMultiplayerGame::BuildArenaCampaignIntroOrder( void )")
    require(order, "const bool teammate = teams && candidate->team == host->team;", "teammates are introduced first")
    card = function_body(game, "void idMultiplayerGame::ShowArenaCampaignIntroCard( idPlayer *subject )")
    for key in ('"#str_42157"', '"#str_42158"', '"#str_42159"', '"#str_42155"', "MPLocalizedTeamName"):
        require(card, key, "introduction cards name the combatant's side")
    fade = function_body(game, "float idMultiplayerGame::ArenaCampaignCeremonyFade( void ) const")
    require(fade, "ARENA_INTRO_DIP_MSEC", "introductions dip briefly instead of fading through each beat")
    clear = function_body(game, "void idMultiplayerGame::ClearArenaCampaignPresentation( void )")
    require(clear, "if ( arenaHudPresentation == 0 ) {", "a clear is only sent over a visible card")
    for event in ("onNamedEvent arenaCampaignIntroduce {", "windowDef oq4_arena_present_intro",
                  'if ( "Desktop::arena_presentClearing" == 1 ) {', 'text	"gui::arena_presentKicker"'):
        require(mp_hud, event, "the legacy HUD's introduction mode and stale-clear guard")
    for token in ('doc.on("arenaCampaignIntroduce"', 'cancel("arena-clear-run")', '"arena-entrance-held-run"',
                  '"opacity": num(1)'):
        require(remastered, token, "the Remastered HUD's introduction mode, clear cancel and victor card text")

    # The entrance and scoreboard present the campaign's game type: a campaign
    # Duel runs as deathmatch and must not be labelled one.
    entrance = function_body(game, "void idMultiplayerGame::BeginArenaCampaignEntrancePresentation( void )")
    for token in ("ArenaCampaignPresentedGameType()", "ArenaCampaignMatchTitle()", '"#str_42156"'):
        require(entrance, token, "the entrance card shows the campaign's own copy")
    require(game, 'scoreBoard->SetStateString( "serverip",\tarenaBoard ? "" : serverAddress.c_str() );',
            "an Arena board has no server address")
    for cvar in ("ui_arenaMatchTitle", "ui_arenaMatchTier", "ui_arenaMatchRole", "ui_arenaMatchGameType"):
        require(arena, f"{cvar}.SetString(", "the campaign publishes its match card for game_mp")

    # The crosshair and status bar stay down during presentation shots.
    require(player, "!spectating && !arenaPresentation && !gameDebug.IsHudActive( DBGHUD_ANY )",
            "Arena presentation shots draw no crosshair or status bar")
    # Over a presentation shot both match HUDs keep only the letterbox, its
    # cards and the centre notice; the overlay HUD stays down too.
    update = function_body(game, "void idMultiplayerGame::UpdateHud( idUserInterface* _mphud )")
    require(update, '_mphud->SetStateBool( "arena_hudHidden", ArenaCampaignPresentationActive() );',
            "the match HUD learns of presentation shots")
    chrome = mp_hud[mp_hud.find("windowDef oq4_match_hud"): mp_hud.find("windowDef mainNotice")]
    require(chrome, 'visible\t( "gui::arena_hudHidden" == 0 )', "the legacy match HUD hides over a presentation shot")
    for window in ("windowDef p_ffa", "windowDef p_ctf", "windowDef fragNotice", "windowDef p_stats",
                   "windowDef oq4_match_context_hud"):
        require(chrome, window, "the legacy match HUD's panels hide with it")
    if "windowDef oq4_arena" in chrome:
        raise AssertionError("the Arena letterbox and cards must stay outside the hidden match HUD")
    require(remastered, 'doc.show(ident, NOT(S("arena_hudHidden")))', "the Remastered match HUD hides over a presentation shot")
    require(remastered, '"main", "display", sel(AND(S("oq4.main")', "the Remastered centre notice stays")
    require(player, "if ( !arenaPresentation ) {", "the overlay HUD stays down over a presentation shot")
    chat = read(ROOT / "src" / "ui" / "ChatWindow.cpp")
    require(chat, 'if (!input && gui->GetStateBool("arena_hudHidden")) { return; }',
            "the passive chat over the classic match HUD hides over a presentation shot")
    # A player killed by the final frag still steers the tableau orbit.
    require(player, "if ( health <= 0 && !arenaFreeLook ) {", "the tableau orbit works for a dead player")
    view = function_body(game, "bool idMultiplayerGame::BuildArenaCampaignPresentationView( idPlayer *viewer, renderView_t *view )")
    require(view, "arenaVictorLookPitch = viewer->viewAngles.pitch;", "the tableau pitch is relative to the latch")

    # A draw is not announced as a defeat.
    announce = function_body(game, "void idMultiplayerGame::ArenaCampaignAnnounceResult( void )")
    for token in ("AS_TEAM_TEAMS_TIED", "AS_DM_YOU_TIED_LEAD"):
        require(announce, token, "a draw gets the stock tie call")

    # Emptied sides and forfeits are not wins: kicking the bots cleared the ladder.
    result = function_body(game, "void idMultiplayerGame::BeginArenaCampaignResult( void )")
    if "arenaResultOutcome = ( forfeitWinner == host->team ) ? ARENA_RESULT_WIN" in result:
        raise AssertionError("an emptied side must not be recorded as an Arena win")
    for token in ("arenaResultOutcome = ARENA_RESULT_ABORTED;", "MP_MATCH_TRANSITION_FORFEIT", "MP_MATCH_TRANSITION_MATCH_ABORTED"):
        require(result, token, "uncontested Arena endings are aborted, not won")
    require(game, "ARENA_RESULT_ABORTED = 3", "the aborted outcome value crosses to the framework")
    require(arena, "ARENA_OUTCOME_ABORTED = 3", "the framework accepts the aborted outcome")
    require(arena, "ARENA_OUTCOME_LOSS, ARENA_OUTCOME_ABORTED, outcome", "the completion parser accepts 0..3")
    reject = function_body(game, "bool idMultiplayerGame::RejectManagedLegacyMutation( const char *action )")
    require(reject, "if ( IsArenaCampaignMatch() ) {", "the menu's admin actions are refused in an Arena match")

    # The single-player match pauses behind its menus, with every deadline rebased.
    require(game_local, "mpGame.RebaseArenaCampaignPauseFrame( Max( 0, msec ) );", "paused Arena frames rebase the match")
    rebase = function_body(game, "void idMultiplayerGame::RebaseArenaCampaignPauseFrame( int deltaMsec )")
    require(rebase, "RebaseCompetitivePauseFrame( deltaMsec );", "the pause shifts the match clock, rounds, bots and respawns")
    run = function_body(game, "void idMultiplayerGame::Run( void )")
    require(run, "if ( IsGameplayFrozen() || arenaPaused ) {", "the rules do not run under the pause")
    require(arena, "ui_arenaMenuOpen.SetBool( menuOpen );", "the campaign publishes the menu-over-match state")

    # Escape and a fresh fire press skip the ceremony instead of risking the result.
    require(game_local, "if ( mpGame.ArenaCampaignOwnsEscape() ) return ESC_IGNORE;", "Escape skips the ceremony")
    skip = function_body(game, "void idMultiplayerGame::UpdateArenaCampaignSkipInput( void )")
    require(skip, "ARENA_SKIP_GRACE_MSEC", "a held trigger cannot skip the result")
    require(sys_cmds, '"openq4_arenaCeremony", Cmd_OpenQ4ArenaCeremony_f, CMD_FL_GAME | CMD_FL_CHEAT',
            "the input-free ceremony driver is a cheat")

    # The stats beat has no chat entry and no "return to the game" line.
    for window in ("windowDef exit_msg", "editDef chat_enter"):
        body = summary[summary.find(window): summary.find(window) + 200]
        require(body, '( "gui::arena_ceremony" == 0 )', f"{window} hides during the Arena stats beat")
    require(header, "bool\t\t\tArenaCampaignPresentationActive( void ) const;", "presentation predicate")


def validate_pause_menu() -> None:
    """Pin the Arena pause menu that replaces the multiplayer menu in a match.

    Escape during a live Arena match opens Resume, Restart match, Settings and
    Leave match - in the stock GUI and under its retained card - instead of the
    multiplayer menu's Join Team, Server Info, Admin and Match Control. The
    world keeps pausing behind it, Restart resets the match in place or falls
    back to the campaign's own launch path (arena_restart_in_place.py guards
    the in-place restart), and Leave takes the existing disconnect to the
    abandoned report.
    """

    import json

    game = read(GAME_LIBS_ROOT / "src" / "mpgame" / "MultiplayerGame.cpp")
    header = read(GAME_LIBS_ROOT / "src" / "mpgame" / "MultiplayerGame.h")
    arena = read(ROOT / "src" / "framework" / "ArenaCampaign.cpp")
    arena_header = read(ROOT / "src" / "framework" / "ArenaCampaign.h")
    session_menu = read(ROOT / "src" / "framework" / "Session_menu.cpp")
    adapter = read(ROOT / "src" / "ui" / "UserInterfaceRetained.cpp")
    pause_gui_path = ROOT / "content" / "baseoq4" / "pak0" / "guis" / "arena_pause.gui"
    pause_gui = read(pause_gui_path)

    # The game opens its own pause menu in mainGui's place, only in an Arena match.
    require(header, "static const int MP_MENU_ARENA_PAUSE = 5;", "the Arena pause menu's currentMenu value")
    start = function_body(game, "idUserInterface* idMultiplayerGame::StartMenu( void )")
    require(start, "if ( currentMenu == 1 && IsArenaCampaignMatch() && arenaPauseGui != NULL ) {",
            "only an Arena match swaps the multiplayer menu for its pause menu")
    require(start, "return arenaPauseGui;", "StartMenu returns the Arena pause menu")
    if start.find("PublishArenaPauseMenu( arenaPauseGui, false );") > start.find("arenaPauseGui->Activate( true, gameLocal.time );"):
        raise AssertionError("the pause menu's match block must be published before its activation")
    require(game, 'arenaPauseGui = uiManager->FindGui( "guis/arena_pause.gui", true, false, true );', "the pause menu loads with the MP GUIs")
    disable = function_body(game, "void idMultiplayerGame::DisableMenu( void )")
    require(disable, "arenaPauseGui->Activate( false, gameLocal.time );", "closing deactivates the pause menu")
    commands = function_body(game, "const char *idMultiplayerGame::HandleArenaPauseCommand( const char *action )")
    for token, reason in (
        ('return "main fromMp_toControls";', "Settings opens the main menu's settings page"),
        ('BufferCommandText( CMD_EXEC_APPEND, "arenaCampaignRestart\\n" );', "Restart asks the campaign"),
        ('BufferCommandText( CMD_EXEC_APPEND, "disconnect\\n" );', "Leave takes the abandoned-report disconnect"),
    ):
        require(commands, token, reason)
    # HandleGuiCommands holds braces in strings, so read the verb's own lines.
    gui_commands = game[game.index('!idStr::Icmp( cmd, "arenaPause" )'):][:600]
    require(gui_commands, "if ( !IsArenaPauseMenuOpen() ) {", "arenaPause commands act only on the open pause menu")
    # Escape still belongs to the ceremony once a result is pending, and the
    # world pause still reads the menu-over-match state the pause menu sets.
    game_local = read(GAME_LIBS_ROOT / "src" / "mpgame" / "Game_local.cpp")
    require(game_local, "if ( mpGame.ArenaCampaignOwnsEscape() ) return ESC_IGNORE;", "the ceremony keeps Escape")
    pauses = function_body(game, "bool idMultiplayerGame::ArenaCampaignPausesWorld( void ) const")
    require(pauses, 'cvarSystem->GetCVarBool( "ui_arenaMenuOpen" )', "the pause menu pauses the match")
    covers = function_body(arena, "static bool ArenaMenuCoversMatch()")
    require(covers, 'idStr::Icmp( gui->Name(), "guis/mpmsgmode.gui" ) != 0', "any menu but chat pauses")

    # The retained card covers the pause menu; the game allows it only there.
    cover = function_body(game, "void idMultiplayerGame::SetRetainedMenuCover( bool covered, idUserInterface *card )")
    require(cover, "const bool arenaPause = IsArenaPauseMenuOpen() && arenaPauseGui != NULL && IsArenaCampaignMatch();",
            "the Arena pause card covers only the Arena pause menu")
    require(cover, 'card->SetStateBool( "mp.cover_allowed", allowed || arenaPause );', "the cover protocol answers for the pause card")
    publish = function_body(game, "void idMultiplayerGame::PublishRetainedMenu( idUserInterface *card )")
    require(publish, "PublishArenaPauseMenu( card, true );", "the card publishes the match block")
    require(session_menu, 'static const char *const RETAINED_ARENA_PAUSE_GUI = "guis/menu/arena_pause.q4ui";', "the Arena pause card")
    require(session_menu, '!idStr::Icmp( gui->Name(), "guis/arena_pause.gui" )', "the session recognises the pause menu")
    request = function_body(session_menu, "void idSessionLocal::HandleRetainedMultiplayerRequest( idUserInterface *gui, const char *request )")
    for verb, command in (("arenaRestart", "restart"), ("arenaLeave", "leave"), ("settings", "settings"), ("resume", "resume")):
        require(request, f'!idStr::Icmp( request, "{verb}" )', f"the card's {verb}")
        require(request, f'"play main_menu_selection ; arenaPause {command}"', f"the card's {verb} is the stock menu's own command")
    for verb in ("arenaRestart", "arenaLeave"):
        require(adapter, f'"{verb}"', "the retained adapter allows the Arena pause verbs")

    # Restart's fallback is the abandoned report's Retry: same token, nothing recorded.
    require(arena_header, "void\t\t\t\t\tRestartMatch();", "the campaign restarts its own matches")
    require(arena, '"arenaCampaignRestart", ArenaCampaign_Restart_f, CMD_FL_SYSTEM,', "the restart command is registered")
    require(arena, 'cmdSystem->RemoveCommand( "arenaCampaignRestart" );', "the restart command is removed with the campaign")
    restart = function_body(arena, "void idArenaCampaign::RestartMatch()")
    order = [restart.find(token) for token in (
        "const int token = ActiveMatchToken();", "sessLocal.Stop();", "AbortMatch();", "OpenBrowser();",
        "ArenaSetResultMatch( state, token );", "state->resultFailure = ARENA_FAILURE_ABANDONED;",
        "if ( !ArenaResultCanRetry( state ) ) {", "RetryResult();")]
    if min(order) < 0 or order != sorted(order):
        raise AssertionError("Restart must leave the match, then retry its token through RetryResult")
    require(restart, "ReportAbandoned( token );", "a match that cannot restart reports itself abandoned")
    # Settings returns to the pause menu, not the main menu's multiplayer home.
    frame = function_body(arena, "void idArenaCampaign::Frame()")
    require(frame, "if ( menuOpen && ArenaMainMenuAtHome() && game != NULL ) {", "the settings page returns to the pause menu")
    require(frame, "idUserInterface *pause = game->StartMenu();", "the pause menu reopens through the game")

    # The stock GUI's actions and questions.
    for window, command in (
        ("arena_pause_resume", "play main_menu_selection arenaPause resume"),
        ("arena_pause_settings", "play main_menu_selection arenaPause settings"),
    ):
        require(gui_window_body(pause_gui, window), command, f"{window} action")
    for window, state in (("arena_pause_restart", '"Desktop::arena_confirm" "1"'), ("arena_pause_leave", '"Desktop::arena_confirm" "2"')):
        require(gui_window_body(pause_gui, window), state, f"{window} asks first")
    yes = gui_window_body(pause_gui, "arena_confirm_yes")
    for command in ("play main_menu_selection arenaPause restart", "play main_menu_selection arenaPause leave"):
        require(yes, command, "the confirmation's YES")
    require(pause_gui, 'set "cmd" "play main_menu_selection arenaPause resume" ;', "Escape resumes")
    require(gui_window_body(pause_gui, "arena_pause_nav"), 'noevents\t( "Desktop::arena_confirm" != 0 )',
            "focus cannot leave an open question for the menu behind it")

    # The retained card's verbs and questions.
    text = (ROOT / "content" / "baseoq4" / "pak0" / "guis" / "menu" / "arena_pause.q4ui").read_text(encoding="utf-8")
    document = json.loads(re.sub(r"^\s*//.*$", "", text, flags=re.M))
    # The card's own verbs; the session's notice layer adds its actions to every retained menu.
    actions = {name: action["arguments"]["command"] for name, action in document["actions"].items()
               if not name.startswith("noticeAction")}
    if actions != {"resume": "resume", "settings": "settings", "arenaRestart": "arenaRestart", "arenaLeave": "arenaLeave"}:
        raise AssertionError(f"the Arena pause card's verbs are {sorted(actions)}")
    if document["events"]["onBack"] != [{"op": "action", "action": "resume"}]:
        raise AssertionError("Back on the Arena pause card resumes")
    # The runtime refuses a document whose bound property has no base value.
    nodes = {}

    def walk(node):
        nodes[node["id"]] = node
        for child in node.get("children", []):
            walk(child)

    walk(document["root"])
    unbased = [binding["id"] for binding in document["bindings"]
               if binding["property"] not in nodes[binding["node"]].get("properties", {})]
    if unbased:
        raise AssertionError(f"the Arena pause card binds properties without base values: {unbased}")
    for modal in ("restartModal", "leaveModal"):
        if f"{modal}Show" not in document["events"]:
            raise AssertionError(f"the Arena pause card asks before {modal}")

    # Every string the menu shows is localized in every shipped table.
    keys = set(re.findall(r"#str_\d+", pause_gui)) | set(re.findall(r"#str_\d+", text))
    own = {f"#str_{value}" for value in range(42180, 42187)}
    if not own <= keys:
        raise AssertionError(f"the pause menu leaves {sorted(own - keys)} unused")
    for path in sorted(STRINGS_DIR.glob("*_openq4.lang")):
        entries = parse_language(path) if path.stem.startswith(("english", "french", "italian", "spanish")) else dict(
            LANG_ENTRY_RE.findall(path.read_text(encoding="utf-8")))
        missing = sorted(key for key in own if not entries.get(key, "").strip())
        if missing:
            raise AssertionError(f"{relative(path)} lacks Arena pause strings {missing}")
    english = parse_language(STRINGS_DIR / "english_openq4.lang")
    for key, expected in (("#str_42180", "RESTART MATCH"), ("#str_42181", "LEAVE MATCH"), ("#str_42186", "MATCH PAUSED")):
        if english[key] != expected:
            raise AssertionError(f"English {key} is {english[key]!r}, expected {expected!r}")

    user_doc = read(ROOT / "docs" / "user" / "arena-campaign.md")
    for token in ("**Resume**", "**Restart match**", "**Settings**", "**Leave match**"):
        require(user_doc, token, "the player guide names the pause menu's actions")


def validate_doc_circuit_table(campaign: Campaign) -> None:
    """The player guide's circuit table is the only human-readable manifest.

    Retargeting a match updates the .cfg and this file's expectation tuples, and
    nothing else notices that the guide still advertises the old map or roster.
    """

    user_doc = read(ROOT / "docs" / "user" / "arena-campaign.md")
    english = parse_language(STRINGS_DIR / "english_openq4.lang")

    rows = [
        [cell.strip() for cell in line.strip().strip("|").split("|")]
        for line in user_doc.splitlines()
        if line.startswith("| ") and "`mp/" in line
    ]
    if len(rows) != 20:
        raise AssertionError(f"circuit table has {len(rows)} match rows, expected 20")

    doc_game_types = {
        "Duel": "Duel",
        "Deathmatch": "DM",
        "Team Deathmatch": "Team DM",
        "Clan Arena": "Clan Arena",
        "Red Rover": "Red Rover",
    }

    index = 0
    for tier in campaign.tiers:
        tier_name = english[tier.name].strip()
        for match in tier.matches:
            row = rows[index]
            index += 1
            where = f"circuit table row {index}"
            if row[0].upper() != tier_name.upper():
                raise AssertionError(f"{where}: tier {row[0]!r} != manifest {tier_name!r}")
            match_name = english[match.name].strip()
            if row[1].upper() != match_name.upper():
                raise AssertionError(f"{where}: match {row[1]!r} != manifest {match_name!r}")
            doc_map = re.search(r"`([^`]+)`", row[2])
            if doc_map is None or doc_map.group(1) != match.map:
                raise AssertionError(f"{where}: map {row[2]!r} != manifest {match.map!r}")
            if doc_game_types.get(row[3]) != match.game_type:
                raise AssertionError(
                    f"{where}: game type {row[3]!r} != manifest {match.game_type!r}"
                )
            doc_roster = [name.strip() for name in row[4].split(",")]
            manifest_roster = [bot.name for bot in match.bots]
            if doc_roster != manifest_roster:
                raise AssertionError(
                    f"{where}: roster {doc_roster} != manifest {manifest_roster}"
                )


def main() -> int:
    campaign = parse_campaign()
    validate_manifest(campaign)
    validate_localization(campaign)
    validate_menu_hooks()
    validate_letterbox()
    validate_transition_ownership()
    validate_engine_hooks()
    validate_game_hooks()
    validate_failure_reporting()
    validate_result_contract()
    validate_best_results()
    validate_ceremony_flow()
    validate_ceremony_presentation()
    validate_docs()
    validate_pause_menu()
    validate_doc_circuit_table(campaign)
    print("arena_campaign: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
