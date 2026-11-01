#!/usr/bin/env python3
"""Guards the multiplayer bot personality contract.

A bot's difficulty, play style and voice are content, not code: three layers of
text file resolve into one flat botTraits_t that the combat code reads.  That
only works while a set of agreements holds between the engine repo, the game
repo and the shipped content, and none of them is anything a compiler can see:

  * The character manager is the first heap-owning bot state, and it is
    initialised and shut down from idGameLocal.  rvBotManager::Init already
    exists, is defined, and is dead code because nobody ever called it - that is
    exactly the failure this pins.
  * Content is read with DECL_LEXER_FLAGS, whose LEXFL_NOFATALERRORS is what
    makes a mod's malformed .bot file a warning instead of a dead server, and
    every ListFiles is paired with a FreeFileList, because the list crosses the
    game module's allocator boundary.
  * The trait field table, the baseline curve and botTraits_t agree.  They are
    three parallel lists in two files; nothing but this notices when one of them
    grows a row and the others do not.
  * The baseline curve actually gets better as the skill number rises.  A sign
    flipped in one row of forty is invisible in review and produces a skill 5
    bot that reacts more slowly than a skill 1 bot.
  * Every shipped character names a style that exists and owns one separate
    .chat file with eight alternatives for every event.  Dialogue may not leak
    back into the mechanics-only .bot files, and no chat line is one the
    broadcast path would silently drop - over the length budget, or starting
    with '#', which the localisation pass would substitute out from under the
    author.
  * Bot chat passes a throttle first.  The engine has no chat flood protection
    anywhere, and a client whose reliable queue overflows is dropped, so
    unthrottled bot chatter can kick real players off a server.

The .style, .bot and .chat files are parsed here by an independent reader
rather than by importing anything from the game, so a parser bug and a content
bug cannot cancel each other out.
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT)).resolve()

BOTFILES = ROOT / "content" / "baseoq4" / "pak0" / "botfiles"
DOC = ROOT / "docs" / "dev" / "mp-bots.md"

# Traits whose curve has a direction: +1 means a higher number is a better
# player, -1 means a lower one is.  Only the vision, aim, trigger and mistake
# traits are listed - the rest are taste, and a test that pinned those would be
# pinning the tuning rather than the contract.
TRAIT_DIRECTION = {
    "sightRange": +1,
    "fov": +1,
    "reactionMsec": -1,
    "reactionVarianceMsec": -1,
    "reacquireMsec": +1,
    "reacquireFraction": -1,
    "peripheralAngle": +1,
    "peripheralPenaltyMsec": -1,
    "turnSpeed": +1,
    "turnAccel": +1,
    "turnDamping": +1,
    "aimTrackTimeConst": -1,
    "aimAnticipation": +1,
    "aimTremorDeg": -1,
    "aimTrackError": -1,
    "aimSettleMsec": -1,
    "aimLead": +1,
    "aimLeadError": -1,
    "fireConeDeg": -1,
    "holdFireTurnRate": -1,
    "mistakeChance": -1,
    "mistakeMsec": -1,
    "dodgeReactMsec": -1,
    "weaponSkill": +1,
    "targetSelection": +1,
    "hearing": +1,
    "itemTiming": +1,
}

# Deliberately flat across the whole curve: these are the axes a style or a
# character owns.  A skill curve that moved them would make every high-skill bot
# play identically, which is the thing the character system exists to stop.
TRAIT_FLAT = (
    "combatRange",
    "aggression",
    "patience",
    "targetStickiness",
    "opportunism",
    "vengefulness",
    "strafeRhythmMsec",
    "strafeRhythmVarianceMsec",
    "weaponSwitchMsec",
    "aimHeight",
    "chatiness",
)

TACTICAL_TRAITS = (
    "initiative",
    "targetStickiness",
    "opportunism",
    "vengefulness",
    "suppressionMsec",
    "strafeRhythmMsec",
    "strafeRhythmVarianceMsec",
    "weaponSwitchMsec",
    "aimHeight",
)

# Triggered replies deliberately use a small, common vocabulary.  The names
# are part of the content contract rather than runtime event enum values: each
# character supplies its own words and responses for these conversational
# intents.
REPLY_CATEGORIES = (
    "help",
    "greeting",
    "thanks",
    "praise",
    "apology",
    "goodGame",
    "challenge",
    "farewell",
    "direct",
)
REPLY_SOURCES = frozenset(("any", "player", "bot"))
REPLY_ADDRESS_MODES = frozenset(("either", "required", "forbidden"))
REPLY_ALLOWED_TOKENS = frozenset(("$self", "$other", "$map"))

# Weapon class names a content file may express an opinion about, taken from the
# preference lists already in Bot.cpp plus the two multiplayer weapons those
# lists leave out.  A misspelled class is silently neutral at runtime.
KNOWN_WEAPONS = frozenset(
    {
        "weapon_blaster",
        "weapon_machinegun",
        "weapon_shotgun",
        "weapon_hyperblaster",
        "weapon_nailgun",
        "weapon_grenadelauncher",
        "weapon_rocketlauncher",
        "weapon_railgun",
        "weapon_lightninggun",
        "weapon_gauntlet",
        "weapon_dmg",
        "weapon_napalmgun",
    }
)


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"{rel(path)} does not exist")
    return path.read_text(encoding="utf-8", errors="replace")


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_regex(haystack: str, pattern: str, context: str) -> re.Match[str]:
    match = re.search(pattern, haystack, re.DOTALL)
    if match is None:
        raise AssertionError(f"Missing /{pattern}/ in {context}")
    return match


def require_order(haystack: str, first: str, second: str, context: str) -> None:
    a = haystack.find(first)
    b = haystack.find(second)
    if a == -1:
        raise AssertionError(f"Missing {first!r} in {context}")
    if b == -1:
        raise AssertionError(f"Missing {second!r} in {context}")
    if a > b:
        raise AssertionError(f"{first!r} must appear before {second!r} in {context}")


def braced_body(source: str, start: int, context: str) -> str:
    """Text between the first '{' at or after start and its matching '}'."""

    open_at = source.find("{", start)
    if open_at == -1:
        raise AssertionError(f"No opening brace for {context}")
    depth = 0
    for i in range(open_at, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[open_at + 1 : i]
    raise AssertionError(f"Unterminated brace section for {context}")


def void_method_body(source: str, qualified_name: str, context: str) -> str:
    """Return a void method body, including sources with #ifdef signatures."""

    definitions = list(
        re.finditer(
            rf"\bvoid\s+{re.escape(qualified_name)}\s*\([^;{{}}]*\)\s*\{{",
            source,
        )
    )
    if not definitions:
        raise AssertionError(f"Missing definition for {context}")

    # RV_UNIFIED_ALLOCATOR selects between two adjacent Init signatures.  The
    # final textual definition owns the shared body and therefore has balanced
    # braces when the inactive preprocessor branch is still present.
    return braced_body(source, definitions[-1].start(), context)


# ----------------------------------------------------------------------------
# An independent reader for the .style / .bot / .chat formats, so a bug in the
# game's parser and a bug in the content cannot agree with each other.
# ----------------------------------------------------------------------------

TOKEN_RE = re.compile(
    r"""
      (?P<comment>//[^\n]*|/\*.*?\*/)
    | (?P<string>"(?:[^"\\\n]|\\.)*")
    | (?P<number>-?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?)
    | (?P<word>[A-Za-z_][A-Za-z0-9_]*)
    | (?P<punct>[{}])
    | (?P<space>\s+)
    """,
    re.VERBOSE | re.DOTALL,
)


class Token:
    __slots__ = ("kind", "text", "line")

    def __init__(self, kind: str, text: str, line: int) -> None:
        self.kind = kind
        self.text = text
        self.line = line

    def __repr__(self) -> str:
        return f"{self.kind}:{self.text}"


def tokenize(source: str, name: str) -> list[Token]:
    tokens: list[Token] = []
    pos = 0
    line = 1
    while pos < len(source):
        match = TOKEN_RE.match(source, pos)
        if match is None:
            raise AssertionError(f"{name}:{line}: cannot read {source[pos:pos + 20]!r}")
        text = match.group(0)
        kind = match.lastgroup
        if kind in ("string", "number", "word", "punct"):
            if kind == "string":
                text = text[1:-1]
            tokens.append(Token(kind, text, line))
        line += text.count("\n")
        pos = match.end()
    return tokens


class Reader:
    def __init__(self, tokens: list[Token], name: str) -> None:
        self.tokens = tokens
        self.name = name
        self.pos = 0

    def at_end(self) -> bool:
        return self.pos >= len(self.tokens)

    def peek(self) -> Token | None:
        return self.tokens[self.pos] if self.pos < len(self.tokens) else None

    def next(self, what: str) -> Token:
        token = self.peek()
        if token is None:
            raise AssertionError(f"{self.name}: file ends where {what} was expected")
        self.pos += 1
        return token

    def expect(self, kind: str, what: str) -> Token:
        token = self.next(what)
        if token.kind != kind:
            raise AssertionError(
                f"{self.name}:{token.line}: expected {what}, found {token.kind} {token.text!r}"
            )
        return token

    def expect_punct(self, text: str) -> Token:
        token = self.next(repr(text))
        if token.kind != "punct" or token.text != text:
            raise AssertionError(
                f"{self.name}:{token.line}: expected {text!r}, found {token.text!r}"
            )
        return token


class Block:
    """One parsed style or character."""

    def __init__(self, kind: str, name: str, source: str) -> None:
        self.kind = kind
        self.name = name
        self.source = source
        self.description = ""
        self.inherit = ""
        self.skill_band: tuple[int, int] | None = None
        self.models: dict[str, str] = {}
        self.traits: list[str] = []
        self.weapons: list[str] = []
        self.skill_blocks: list[int] = []
        self.chat: dict[str, list[tuple[str, int]]] = {}
        self.replies: list[ReplyRule] = []


class ReplyRule:
    """One named trigger/reply rule inside a characterChat block."""

    def __init__(self, name: str, line: int) -> None:
        self.name = name
        self.line = line
        self.priority: int | None = None
        self.source: str | None = None
        self.addressed: str | None = None
        self.triggers: list[tuple[str, int]] = []
        self.lines: list[tuple[str, int]] = []


MOD_WORDS = ("set", "add", "scale")


def parse_chat_block(reader: Reader, block: Block) -> None:
    event = reader.expect("word", "a chat event name").text
    reader.expect_punct("{")
    lines: list[tuple[str, int]] = []
    while True:
        line_token = reader.next("a chat line or '}'")
        if line_token.kind == "punct" and line_token.text == "}":
            break
        if line_token.kind != "string":
            raise AssertionError(
                f"{reader.name}:{line_token.line}: chat lines must be quoted, "
                f"found {line_token.text!r}"
            )
        lines.append((line_token.text, line_token.line))
    block.chat.setdefault(event, []).extend(lines)


def parse_reply_block(reader: Reader, block: Block) -> None:
    """Read `reply <name> { ... }` without sharing the game's parser."""

    name = reader.expect("word", "a reply rule name")
    rule = ReplyRule(name.text, name.line)
    reader.expect_punct("{")

    while True:
        token = reader.next("a reply setting, quoted line or '}'")
        if token.kind == "punct" and token.text == "}":
            break
        if token.kind == "string":
            rule.lines.append((token.text, token.line))
            continue
        if token.kind != "word":
            raise AssertionError(
                f"{reader.name}:{token.line}: expected a reply setting or quoted line, "
                f"found {token.text!r}"
            )

        if token.text == "priority":
            value = reader.expect("number", "an integer reply priority")
            if not re.fullmatch(r"-?\d+", value.text):
                raise AssertionError(
                    f"{reader.name}:{value.line}: reply priority must be an integer, "
                    f"found {value.text!r}"
                )
            if rule.priority is not None:
                raise AssertionError(
                    f"{reader.name}:{token.line}: reply {rule.name!r} repeats priority"
                )
            rule.priority = int(value.text)
        elif token.text == "source":
            value = reader.expect("word", "any, player or bot")
            if rule.source is not None:
                raise AssertionError(
                    f"{reader.name}:{token.line}: reply {rule.name!r} repeats source"
                )
            rule.source = value.text
        elif token.text == "addressed":
            value = reader.expect("word", "either, required or forbidden")
            if rule.addressed is not None:
                raise AssertionError(
                    f"{reader.name}:{token.line}: reply {rule.name!r} repeats addressed"
                )
            rule.addressed = value.text
        elif token.text == "trigger":
            value = reader.expect("string", "a quoted trigger phrase")
            rule.triggers.append((value.text, value.line))
        else:
            raise AssertionError(
                f"{reader.name}:{token.line}: unknown reply key {token.text!r}"
            )

    block.replies.append(rule)


def parse_statements(reader: Reader, block: Block, skill_levels: int, nested: bool) -> None:
    while True:
        token = reader.next("a key or '}'")
        if token.kind == "punct" and token.text == "}":
            return
        if token.kind != "word":
            raise AssertionError(
                f"{reader.name}:{token.line}: expected a key, found {token.text!r}"
            )

        key = token.text

        if key in MOD_WORDS:
            block.traits.append(reader.expect("word", "a trait name").text)
            reader.expect("number", f"a number for '{key}'")
            continue

        if key == "weapon":
            block.weapons.append(reader.expect("string", "a weapon class name").text)
            reader.expect("number", "a weapon bias")
            continue

        if key == "skill":
            level = reader.expect("number", "a skill level")
            if nested:
                raise AssertionError(
                    f"{reader.name}:{level.line}: skill blocks may not be nested"
                )
            if not re.fullmatch(r"\d+", level.text) or not 1 <= int(level.text) <= skill_levels:
                raise AssertionError(
                    f"{reader.name}:{level.line}: skill level {level.text!r} is outside 1..{skill_levels}"
                )
            block.skill_blocks.append(int(level.text))
            reader.expect_punct("{")
            parse_statements(reader, block, skill_levels, nested=True)
            continue

        if key == "description":
            block.description = reader.expect("string", "a description").text
            continue

        if key == "inherit":
            block.inherit = reader.expect("string", "a style name").text
            continue

        if block.kind == "character":
            if key == "skillBand":
                low = reader.expect("number", "the lowest skill")
                high = reader.expect("number", "the highest skill")
                block.skill_band = (int(float(low.text)), int(float(high.text)))
                continue
            if key in ("model", "modelMarine", "modelStrogg"):
                block.models[key] = reader.expect("string", "a playerModel decl name").text
                continue
            if key == "chat":
                parse_chat_block(reader, block)
                continue

        raise AssertionError(
            f"{reader.name}:{token.line}: unknown key {key!r} in a {block.kind} file"
        )


def parse_file(path: Path, kind: str, skill_levels: int) -> Block:
    name = rel(path)
    reader = Reader(tokenize(read(path), name), name)
    header = reader.expect("word", f"'{kind}'")
    if header.text != kind:
        raise AssertionError(f"{name}:{header.line}: expected '{kind}', found {header.text!r}")
    block = Block(kind, reader.expect("string", f"the {kind} name").text, name)
    reader.expect_punct("{")
    parse_statements(reader, block, skill_levels, nested=False)
    if not reader.at_end():
        token = reader.peek()
        raise AssertionError(f"{name}:{token.line}: trailing {token.text!r} after the block")
    return block


def parse_chat_file(path: Path) -> Block:
    """Read one `characterChat "<owner>" { chat <event> { ... } }` file."""

    name = rel(path)
    reader = Reader(tokenize(read(path), name), name)
    header = reader.expect("word", "'characterChat'")
    if header.text != "characterChat":
        raise AssertionError(
            f"{name}:{header.line}: expected 'characterChat', found {header.text!r}"
        )

    block = Block(
        "characterChat",
        reader.expect("string", "the owning character name").text,
        name,
    )
    reader.expect_punct("{")

    while True:
        token = reader.next("'chat', 'reply' or '}'")
        if token.kind == "punct" and token.text == "}":
            break
        if token.kind != "word":
            raise AssertionError(
                f"{name}:{token.line}: expected 'chat' or 'reply', found {token.text!r}"
            )
        if token.text == "chat":
            parse_chat_block(reader, block)
        elif token.text == "reply":
            parse_reply_block(reader, block)
        else:
            raise AssertionError(
                f"{name}:{token.line}: expected 'chat' or 'reply', found {token.text!r}"
            )

    if not reader.at_end():
        token = reader.peek()
        raise AssertionError(f"{name}:{token.line}: trailing {token.text!r} after the block")

    return block


Q4_COLOR_RE = re.compile(r"\^(?:[cC][0-9]{3}|[rR]|[^^])")
REPLY_WORD_RE = re.compile(r"[a-z0-9]+")


def normalize_reply_words(text: str) -> tuple[str, ...]:
    """Mirror the runtime's colour-free, ASCII, case-insensitive word view."""

    without_colors = Q4_COLOR_RE.sub("", text)
    return tuple(REPLY_WORD_RE.findall(without_colors.lower()))


def normalize_reply_phrase(text: str) -> str:
    return " ".join(normalize_reply_words(text))


def reply_phrase_matches(trigger: str, message: str) -> bool:
    """Contiguous whole-word phrase matching, never substring matching."""

    needle = normalize_reply_words(trigger)
    words = normalize_reply_words(message)
    if not needle or len(needle) > len(words):
        return False
    return any(words[index : index + len(needle)] == needle
               for index in range(len(words) - len(needle) + 1))


def validate_reply_matcher_vectors() -> None:
    """Pin the player-facing matching semantics independently of C++."""

    vectors = (
        ("hello", "^1HeLLo, ^7Voss!", True, "colours, case and punctuation"),
        ("hi", "HI!", True, "case-insensitive exact word"),
        ("hi", "this should not match", False, "short word is not a substring"),
        ("hi", "a high ledge", False, "word prefix is not a match"),
        ("good game", "Well, GOOD... GAME!", True, "punctuated phrase"),
        ("good game", "that was a good gamer", False, "phrase final boundary"),
        ("good game", "good very game", False, "phrase words stay contiguous"),
        ("gg", "egg on your face", False, "abbreviation is a whole word"),
    )
    for trigger, message, expected, context in vectors:
        actual = reply_phrase_matches(trigger, message)
        if actual != expected:
            raise AssertionError(
                f"reply matcher failed {context}: trigger {trigger!r}, "
                f"message {message!r}, expected {expected}, got {actual}"
            )


# ----------------------------------------------------------------------------
# Validators
# ----------------------------------------------------------------------------


def header_constants(header: str) -> dict[str, int]:
    constants: dict[str, int] = {}
    for name in (
        "BOT_SKILL_LEVELS",
        "BOT_MAX_WEAPON_BIAS",
        "BOT_CHAT_MAX_LEN",
        "BOT_MAX_REPLY_RULES",
        "BOT_MAX_REPLY_TRIGGERS",
        "BOT_MAX_REPLY_LINES",
        "BOT_REPLY_TRIGGER_MAX_LEN",
    ):
        match = re.search(rf"\b{name}\s*=\s*(\d+)\s*;", header)
        if match is None:
            raise AssertionError(f"{name} is not defined in BotCharacter.h")
        constants[name] = int(match.group(1))
    return constants


def trait_fields(header: str) -> list[str]:
    body = header[header.index("typedef struct botTraits_s {") : header.index("} botTraits_t;")]
    return re.findall(r"^\tfloat\t+(\w+);", body, re.MULTILINE)


def chat_event_enum(header: str) -> list[str]:
    body = header[header.index("typedef enum rvBotChatEvent_e {") : header.index("} rvBotChatEvent;")]
    return [name for name in re.findall(r"\b(BOTCHAT_\w+)\b", body) if name != "BOTCHAT_NUM"]


def validate_bot_manager_lifecycle(game_local: str) -> None:
    init = void_method_body(game_local, "idGameLocal::Init", "idGameLocal::Init")
    require(init, "botCharacterManager.Init();", "idGameLocal::Init")

    shutdown = void_method_body(
        game_local,
        "idGameLocal::Shutdown",
        "idGameLocal::Shutdown",
    )
    require(shutdown, "botCharacterManager.Shutdown();", "idGameLocal::Shutdown")


def require_bot_lifecycle_mutation_rejected(
    game_local: str,
    removed_call: str,
    context: str,
) -> None:
    mutant = game_local.replace(removed_call, "", 1)
    if mutant == game_local:
        raise AssertionError(f"Mutation setup could not remove {removed_call!r}")
    try:
        validate_bot_manager_lifecycle(mutant)
    except AssertionError:
        return
    raise AssertionError(f"Bot lifecycle validator accepted missing {context}")


def validate_wiring() -> None:
    mp = GAME_LIBS_ROOT / "src" / "mpgame"
    if not mp.is_dir():
        print(f"mp_bot_characters: skipped game checks (no GameLibs checkout at {GAME_LIBS_ROOT})")
        return

    # rvBot gains a character pointer and a trait struct, so the contract header
    # has to be visible before Bot.h is read.
    game_local_h = read(mp / "Game_local.h")
    require_order(
        game_local_h,
        '#include "bots/BotCharacter.h"',
        '#include "bots/Bot.h"',
        "Game_local.h include order",
    )

    # rvBotManager::Init has been defined and uncalled since the bots landed.
    # The character manager owns heap memory, so this one has to be wired.
    game_local = read(mp / "Game_local.cpp")
    validate_bot_manager_lifecycle(game_local)
    require_bot_lifecycle_mutation_rejected(
        game_local,
        "botCharacterManager.Init();",
        "bot character initialization",
    )
    require_bot_lifecycle_mutation_rejected(
        game_local,
        "botCharacterManager.Shutdown();",
        "bot character shutdown",
    )

    # A bot speaks through the same call the server makes for a human's say, so
    # the line is indistinguishable from a player's.  It ships private.
    mp_header = read(mp / "MultiplayerGame.h")
    at = mp_header.index("ProcessChatMessage")
    access = None
    for match in re.finditer(r"^\s*(public|protected|private)\s*:", mp_header[:at], re.MULTILINE):
        access = match.group(1)
    if access != "public":
        raise AssertionError(
            f"idMultiplayerGame::ProcessChatMessage is {access}; rvBot cannot reach it"
        )


def validate_manager() -> None:
    mp = GAME_LIBS_ROOT / "src" / "mpgame"
    if not mp.is_dir():
        return

    source = read(mp / "bots" / "BotCharacter.cpp")

    # A mod's malformed character file must warn, not kill the server.
    # LEXFL_NOFATALERRORS is what buys that, and it comes with these flags.
    # Every reader needs it, so check every reader rather than just finding the
    # token once - styles, characters and voices are loaded down separate paths.
    flags = [arg.strip() for arg in re.findall(r"SetFlags\(\s*([^)]*)\)", source)]
    if not flags:
        raise AssertionError(
            "BotCharacter.cpp never sets the lexer flags, so a malformed character file in a "
            "mod is a fatal engine error instead of a warning"
        )
    wrong = [arg for arg in flags if arg != "DECL_LEXER_FLAGS"]
    if wrong:
        raise AssertionError(
            f"BotCharacter.cpp reads content with {wrong} rather than DECL_LEXER_FLAGS, whose "
            "LEXFL_NOFATALERRORS is what keeps bad content from killing a server"
        )

    # The file list crosses the game module's allocator boundary.
    listed = source.count("->ListFiles(")
    freed = source.count("->FreeFileList(")
    if listed == 0:
        raise AssertionError(
            "BotCharacter.cpp never calls ListFiles; characters in a pk4 or a mod would be invisible"
        )
    if listed != freed:
        raise AssertionError(
            f"BotCharacter.cpp has {listed} ListFiles calls and {freed} FreeFileList calls"
        )

    for needle in (
        '"botfiles/styles"',
        '"botfiles/characters"',
        '"botfiles/chats"',
        '".style"',
        '".bot"',
        '".chat"',
    ):
        require(source, needle, "rvBotCharacterManager::Init enumeration")

    require_order(
        source,
        "LoadCharacterFile( path.c_str() );",
        "LoadChatFile( path.c_str() );",
        "rvBotCharacterManager::Init character/chat load order",
    )

    # Separate voice banks are merged by owner after every character exists.
    # Keep this check on code rather than comments: case-sensitive ownership
    # would silently strand a perfectly valid `characterChat "anderson"` block,
    # and parsing directly into the live character would leave its early lines
    # behind when a later line or closing brace is malformed.
    code = strip_comments(source)
    load_chat_at = code.index("bool rvBotCharacterManager::LoadChatFile")
    resolve_at = code.index("void rvBotCharacterManager::ResolveInheritance", load_chat_at)
    load_chat = code[load_chat_at:resolve_at]
    require(load_chat, "idStr::Icmp(", "rvBotCharacterManager::LoadChatFile owner lookup")
    require(
        load_chat,
        "SkipBracedSection( false )",
        "rvBotCharacterManager::LoadChatFile unknown-owner recovery",
    )
    require(
        load_chat,
        "stagedChat.ParseChatBlock(",
        "rvBotCharacterManager::LoadChatFile atomic block parse",
    )
    require(
        load_chat,
        "stagedChat.ParseReplyBlock(",
        "rvBotCharacterManager::LoadChatFile atomic reply parse",
    )
    require_order(
        load_chat,
        "if ( !closed )",
        "character->chat[event].Append(",
        "rvBotCharacterManager::LoadChatFile atomic block commit",
    )
    require_order(
        load_chat,
        "if ( !closed )",
        "character->replies.Append(",
        "rvBotCharacterManager::LoadChatFile atomic reply commit",
    )

    # Reply rules are mod content too.  Their parser must enforce the same
    # bounded-memory and non-fatal contracts as the event-line parser.
    reply_parse_at = code.index("bool rvBotCharacter::ParseReplyBlock")
    character_parse_at = code.index("bool rvBotCharacter::Parse(", reply_parse_at)
    reply_parse = code[reply_parse_at:character_parse_at]
    for constant in (
        "BOT_MAX_REPLY_RULES",
        "BOT_MAX_REPLY_TRIGGERS",
        "BOT_MAX_REPLY_LINES",
        "BOT_REPLY_TRIGGER_MAX_LEN",
    ):
        require(reply_parse, constant, "rvBotCharacter::ParseReplyBlock content cap")
    require(
        reply_parse,
        "BotReplyLineTokensValid(",
        "rvBotCharacter::ParseReplyBlock reply token validation",
    )
    require(
        reply_parse,
        "NormalizeReplyText(",
        "rvBotCharacter::ParseReplyBlock trigger normalization",
    )

    # Shipped dialogue is separate, but existing add-ons may still carry
    # inline `chat` blocks in a character.  Both paths must share the same
    # parser so validation and line-length behaviour cannot drift.
    character_parse_at = code.index("bool rvBotCharacter::Parse(")
    character_apply_at = code.index("void rvBotCharacter::Apply", character_parse_at)
    character_parse = code[character_parse_at:character_apply_at]
    require(
        character_parse,
        'token.Icmp( "chat" )',
        "rvBotCharacter legacy inline chat compatibility",
    )
    require(
        character_parse,
        "ParseChatBlock( lexer, sourceName )",
        "rvBotCharacter legacy inline chat compatibility",
    )

    # The dead Quake 3 prototypes share the botfiles tree; nothing may read them.
    for dead in ('"botfiles/bots"', '"botfiles/items.c"', '"botfiles/weapons.c"'):
        if dead in source:
            raise AssertionError(f"BotCharacter.cpp reads {dead}, which is a dead Quake 3 prototype")

    # Unbounded bot chat can kick real players: the engine drops a client whose
    # reliable queue overflows, and nothing in the chat path rate limits.
    for constant in ("BOT_CHAT_CLIENT_THROTTLE_MSEC", "BOT_CHAT_GLOBAL_THROTTLE_MSEC"):
        require(source, constant, "BotCharacter.cpp chat throttle")

    header = read(mp / "bots" / "BotCharacter.h")
    constants = header_constants(header)
    fields = trait_fields(header)

    # The trait table is how a content file names a trait.  Every row has to
    # point at a real field, and every field wants a row or it can never be
    # tuned from content.
    validate_baseline(trait_table(source, fields, constants["BOT_SKILL_LEVELS"]),
                      constants["BOT_SKILL_LEVELS"])


NUMBER_RE = re.compile(r"(?<![A-Za-z0-9_.])[-+]?\d+(?:\.\d*)?(?:[eE][-+]?\d+)?[fF]?(?![A-Za-z0-9_.])")


def strip_comments(text: str) -> str:
    return re.sub(r"//[^\n]*", " ", re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL))


def numbers_in(text: str) -> list[float]:
    return [float(token.rstrip("fF")) for token in NUMBER_RE.findall(text)]


def split_rows(body: str) -> list[str]:
    """Split a C initialiser body at its top-level commas."""

    rows: list[str] = []
    current: list[str] = []
    depth = 0
    for ch in body:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            rows.append("".join(current))
            current = []
        else:
            current.append(ch)
    rows.append("".join(current))
    return [row for row in rows if row.strip()]


def row_name(row: str) -> str:
    """The trait a table row is about, whether it is written as a macro or a struct."""

    macro = re.match(r"\s*[A-Z][A-Z0-9_]*\(\s*(\w+)\s*[,)]", row)
    if macro is not None:
        return macro.group(1)
    quoted = re.search(r'"(\w+)"', row)
    return quoted.group(1) if quoted is not None else ""


def trait_table(source: str, fields: list[str], levels: int) -> dict[str, list[float]]:
    """{trait: [value at skill 1..N]} out of whatever table BotCharacter.cpp writes it in.

    The layout is the implementation's business - one row per trait carrying its
    own curve, or a field table beside a parallel curve array - so this finds
    whichever array names the most traits and reads the numbers out of it rather
    than insisting on a shape.
    """

    known = set(fields)
    best: list[str] = []
    for match in re.finditer(r"\w+\s+\w+\s*\[\s*\]\s*=", source):
        rows = split_rows(strip_comments(braced_body(source, match.end(), "a trait table")))
        if sum(1 for row in rows if row_name(row) in known) > sum(
            1 for row in best if row_name(row) in known
        ):
            best = rows
    named = [row_name(row) for row in best]
    if not [name for name in named if name in known]:
        raise AssertionError(
            "BotCharacter.cpp has no table naming the botTraits_t fields, so no style or "
            "character file can reach any trait by name"
        )

    unknown = [name for name in named if name not in known]
    if unknown:
        raise AssertionError(
            f"The trait table names {unknown}, which are not fields of botTraits_t"
        )
    missing = [name for name in fields if name not in named]
    if missing:
        raise AssertionError(
            f"botTraits_t fields {missing} have no row in the trait table, so no style or "
            "character file can ever reach them"
        )

    curve: dict[str, list[float]] = {}
    for name, row in zip(named, best):
        values = numbers_in(row)
        if len(values) == levels:
            curve[name] = values
        elif len(values) == levels + 2:
            # The row carries its clamp as well.  A baseline value outside its
            # own clamp is clamped away on the first resolve and the tuning
            # never takes effect, so check it while the numbers are in hand.
            low, high, values = values[0], values[1], values[2:]
            outside = [value for value in values if not low <= value <= high]
            if outside:
                raise AssertionError(
                    f"The baseline for {name!r} has {outside} outside its own clamp "
                    f"of {low}..{high}, which is silently clamped away on the first resolve"
                )
            curve[name] = values
        elif len(values) < levels:
            return separate_curve(source, fields, levels)
        else:
            raise AssertionError(
                f"The trait table row for {name!r} carries {len(values)} numbers; "
                f"expected {levels} or {levels + 2}"
            )
    return curve


def separate_curve(source: str, fields: list[str], levels: int) -> dict[str, list[float]]:
    """The layout where the field table and the skill curve are parallel arrays."""

    match = re.search(r"\bfloat\s+\w+\s*\[[^\]]*\]\s*\[[^\]]*\]\s*=", source)
    if match is None:
        raise AssertionError(
            "The trait table carries no skill curve and there is no separate curve array, "
            "so nothing can check that skill 5 is better than skill 1"
        )
    rows = [
        numbers_in(row)
        for row in split_rows(strip_comments(braced_body(source, match.end(), "the skill curve")))
    ]
    if len(rows) == len(fields) and all(len(values) == levels for values in rows):
        return {fields[i]: rows[i] for i in range(len(fields))}
    if len(rows) == levels and all(len(values) == len(fields) for values in rows):
        return {fields[i]: [rows[level][i] for level in range(levels)] for i in range(len(fields))}
    raise AssertionError(
        f"The skill curve is {len(rows)} rows of {sorted({len(v) for v in rows})} numbers; "
        f"expected {len(fields)} rows of {levels} or {levels} rows of {len(fields)}"
    )


def validate_baseline(curve: dict[str, list[float]], levels: int) -> None:
    for name, direction in TRAIT_DIRECTION.items():
        if name not in curve:
            raise AssertionError(f"The baseline curve has no row for {name!r}")
        values = curve[name]
        ordered = values if direction > 0 else list(reversed(values))
        for level in range(1, levels):
            if ordered[level] <= ordered[level - 1]:
                raise AssertionError(
                    f"The baseline curve for {name!r} is {values}, which does not improve "
                    f"monotonically from skill 1 to skill {levels}"
                )
        # Stated separately because it is the claim the documentation makes.
        if direction > 0 and not values[0] < values[-1]:
            raise AssertionError(f"{name!r} is not better at skill {levels} than at skill 1")
        if direction < 0 and not values[0] > values[-1]:
            raise AssertionError(f"{name!r} is not better at skill {levels} than at skill 1")

    for name in TRAIT_FLAT:
        if name not in curve:
            raise AssertionError(f"The baseline curve has no row for {name!r}")
        if len(set(curve[name])) != 1:
            raise AssertionError(
                f"{name!r} is {curve[name]} across the skill curve, but it is a style and "
                "character axis and must stay flat, or every high-skill bot plays the same"
            )

    # The composites Bot.cpp actually fires with: initiative bends the raw
    # reaction and cone rows, and the bend is bounded so the effective curve
    # still improves with skill.  Before the bound the raw rows passed while a
    # skill-1 rusher reacted faster than a plain skill-4 bot.
    initiative = curve["initiative"]
    reaction = [value * (1.25 - 0.5 * min(1.0, max(0.0, eager)))
                for value, eager in zip(curve["reactionMsec"], initiative)]
    cone = [value * (0.5 + eager) for value, eager in zip(curve["fireConeDeg"], initiative)]
    for name, values in (("effective reaction", reaction), ("effective fire cone", cone)):
        for level in range(1, levels):
            if values[level] >= values[level - 1]:
                raise AssertionError(
                    f"The {name} is {[round(v, 1) for v in values]}, which does not shrink "
                    f"from skill 1 to skill {levels}"
                )


def validate_content() -> None:
    header_path = GAME_LIBS_ROOT / "src" / "mpgame" / "bots" / "BotCharacter.h"
    if not header_path.is_file():
        print(f"mp_bot_characters: skipped content checks (no BotCharacter.h at {header_path})")
        return

    header = read(header_path)
    constants = header_constants(header)
    levels = constants["BOT_SKILL_LEVELS"]
    fields = set(trait_fields(header))
    events = chat_event_enum(header)

    manager = GAME_LIBS_ROOT / "src" / "mpgame" / "bots" / "BotCharacter.cpp"
    words = chat_event_words(manager, len(events))

    style_dir = BOTFILES / "styles"
    character_dir = BOTFILES / "characters"
    chat_dir = BOTFILES / "chats"
    if not style_dir.is_dir():
        raise AssertionError(f"{rel(style_dir)} does not exist")
    if not character_dir.is_dir():
        raise AssertionError(f"{rel(character_dir)} does not exist")
    if not chat_dir.is_dir():
        raise AssertionError(f"{rel(chat_dir)} does not exist")

    styles = {}
    for path in sorted(style_dir.glob("*.style")):
        block = parse_file(path, "style", levels)
        styles[block.name.lower()] = block
    if len(styles) < 6:
        raise AssertionError(f"{rel(style_dir)} holds {len(styles)} styles; six were designed")

    characters = [parse_file(path, "character", levels) for path in sorted(character_dir.glob("*.bot"))]
    if len(characters) < 10:
        raise AssertionError(
            f"{rel(character_dir)} holds {len(characters)} characters; ten is the floor"
        )
    chat_blocks = [parse_chat_file(path) for path in sorted(chat_dir.glob("*.chat"))]

    seen: dict[str, str] = {}
    for block in list(styles.values()) + characters:
        check_block(block, fields, constants)

        if block.kind == "character":
            key = block.name.lower()
            if key in seen:
                raise AssertionError(
                    f"{block.source}: character {block.name!r} is also declared in {seen[key]}"
                )
            seen[key] = block.source

            if not block.inherit:
                raise AssertionError(f"{block.source}: character {block.name!r} names no style")
            if block.inherit.lower() not in styles:
                raise AssertionError(
                    f"{block.source}: character {block.name!r} inherits style "
                    f"{block.inherit!r}, which no .style file declares"
                )
            if block.skill_band is None:
                raise AssertionError(f"{block.source}: character {block.name!r} has no skillBand")
            low, high = block.skill_band
            if not 1 <= low <= high <= levels:
                raise AssertionError(
                    f"{block.source}: skillBand {low} {high} is outside 1..{levels}"
                )

            if block.chat:
                raise AssertionError(
                    f"{block.source}: character mechanics files may not contain chat blocks; "
                    f"move {block.name!r}'s dialogue to botfiles/chats"
                )
        elif block.inherit and block.inherit.lower() not in styles:
            raise AssertionError(
                f"{block.source}: style {block.name!r} inherits {block.inherit!r}, "
                "which no .style file declares"
            )

    expected_events = set(words)
    allowed_tokens = {
        word: {"$self", "$map"}
        for word in words
    }
    for word in ("kill", "killGauntlet", "killStreak", "revenge", "death"):
        allowed_tokens[word].update(("$other", "$weapon"))
    allowed_tokens["itemDenied"].update(("$other", "$item"))
    allowed_tokens["leadTaken"].add("$other")
    allowed_tokens["leadLost"].add("$other")
    allowed_tokens["order"].add("$other")

    chats_by_owner: dict[str, Block] = {}
    normalized_lines: dict[str, tuple[str, str, int]] = {}
    normalized_reply_lines: dict[str, tuple[str, str, int]] = {}
    for block in chat_blocks:
        check_block(block, fields, constants)
        owner = block.name.lower()

        if owner in chats_by_owner:
            raise AssertionError(
                f"{block.source}: character {block.name!r} also owns "
                f"{chats_by_owner[owner].source}"
            )
        chats_by_owner[owner] = block

        if owner not in seen:
            raise AssertionError(
                f"{block.source}: chat owner {block.name!r} has no matching character"
            )

        file_stem = Path(block.source).stem.lower()
        if file_stem != owner:
            raise AssertionError(
                f"{block.source}: file name must match its chat owner {block.name!r}"
            )

        declared_events = set(block.chat)
        unknown = sorted(declared_events - expected_events)
        missing = sorted(expected_events - declared_events)
        if unknown:
            raise AssertionError(f"{block.source}: unknown chat events {unknown}")
        if missing:
            raise AssertionError(
                f"{block.source}: character {block.name!r} has nothing to say for {missing}"
            )

        for event in words:
            lines = block.chat[event]
            if len(lines) != 8:
                raise AssertionError(
                    f"{block.source}: chat {event} has {len(lines)} lines; "
                    "the shipped voice-bank target is exactly 8"
                )

            token_free = 0
            for text, line in lines:
                tokens = set(re.findall(r"\$[A-Za-z_][A-Za-z0-9_]*", text))
                invalid = sorted(tokens - allowed_tokens[event])
                if invalid:
                    raise AssertionError(
                        f"{block.source}:{line}: chat {event} uses unavailable tokens {invalid}"
                    )
                if "$other" not in tokens:
                    token_free += 1

                normalized = re.sub(r"[^a-z0-9$]+", " ", text.lower()).strip()
                previous = normalized_lines.get(normalized)
                if previous is not None:
                    previous_owner, previous_event, previous_line = previous
                    raise AssertionError(
                        f"{block.source}:{line}: {block.name}/{event} repeats the line at "
                        f"{previous_owner}/{previous_event}:{previous_line}"
                    )
                normalized_lines[normalized] = (block.name, event, line)

            if event in ("leadTaken", "leadLost") and token_free < 4:
                raise AssertionError(
                    f"{block.source}: chat {event} needs at least four lines that do not "
                    "require $other, because the lead event may have no named rival"
                )

        if len(block.replies) > constants["BOT_MAX_REPLY_RULES"]:
            raise AssertionError(
                f"{block.source}: {len(block.replies)} reply rules exceed "
                f"BOT_MAX_REPLY_RULES ({constants['BOT_MAX_REPLY_RULES']})"
            )

        replies_by_name: dict[str, ReplyRule] = {}
        normalized_triggers: dict[str, tuple[str, int]] = {}
        for rule in block.replies:
            if rule.name in replies_by_name:
                previous = replies_by_name[rule.name]
                raise AssertionError(
                    f"{block.source}:{rule.line}: reply category {rule.name!r} is also "
                    f"declared on line {previous.line}"
                )
            replies_by_name[rule.name] = rule

            if rule.priority is None:
                raise AssertionError(
                    f"{block.source}:{rule.line}: reply {rule.name!r} declares no priority"
                )
            if not 0 <= rule.priority <= 100:
                raise AssertionError(
                    f"{block.source}:{rule.line}: reply {rule.name!r} priority "
                    f"{rule.priority} is outside the runtime's 0..100 range"
                )
            if rule.source not in REPLY_SOURCES:
                raise AssertionError(
                    f"{block.source}:{rule.line}: reply {rule.name!r} has source "
                    f"{rule.source!r}, expected one of {sorted(REPLY_SOURCES)}"
                )
            if rule.addressed not in REPLY_ADDRESS_MODES:
                raise AssertionError(
                    f"{block.source}:{rule.line}: reply {rule.name!r} has addressed "
                    f"{rule.addressed!r}, expected one of {sorted(REPLY_ADDRESS_MODES)}"
                )
            if not rule.triggers:
                raise AssertionError(
                    f"{block.source}:{rule.line}: reply {rule.name!r} declares no triggers"
                )
            if len(rule.triggers) > constants["BOT_MAX_REPLY_TRIGGERS"]:
                raise AssertionError(
                    f"{block.source}:{rule.line}: reply {rule.name!r} has "
                    f"{len(rule.triggers)} triggers, over BOT_MAX_REPLY_TRIGGERS "
                    f"({constants['BOT_MAX_REPLY_TRIGGERS']})"
                )
            if len(rule.lines) > constants["BOT_MAX_REPLY_LINES"]:
                raise AssertionError(
                    f"{block.source}:{rule.line}: reply {rule.name!r} has "
                    f"{len(rule.lines)} lines, over BOT_MAX_REPLY_LINES "
                    f"({constants['BOT_MAX_REPLY_LINES']})"
                )

            for trigger, line in rule.triggers:
                normalized = normalize_reply_phrase(trigger)
                if not normalized:
                    raise AssertionError(
                        f"{block.source}:{line}: reply {rule.name!r} trigger {trigger!r} "
                        "contains no matchable words"
                    )
                if len(normalized) > constants["BOT_REPLY_TRIGGER_MAX_LEN"]:
                    raise AssertionError(
                        f"{block.source}:{line}: reply {rule.name!r} trigger is "
                        f"{len(normalized)} characters after normalization, over "
                        f"BOT_REPLY_TRIGGER_MAX_LEN "
                        f"({constants['BOT_REPLY_TRIGGER_MAX_LEN']})"
                    )
                previous = normalized_triggers.get(normalized)
                if previous is not None:
                    previous_rule, previous_line = previous
                    raise AssertionError(
                        f"{block.source}:{line}: reply {rule.name!r} trigger {trigger!r} "
                        f"normalizes to the trigger already used by {previous_rule!r} on "
                        f"line {previous_line}"
                    )
                normalized_triggers[normalized] = (rule.name, line)

            expected_line_count = 2 if owner == "kane" else 4
            if len(rule.lines) != expected_line_count:
                raise AssertionError(
                    f"{block.source}:{rule.line}: reply {rule.name!r} has "
                    f"{len(rule.lines)} lines; shipped characters require "
                    f"{expected_line_count}"
                )

            for text, line in rule.lines:
                tokens = set(re.findall(r"\$[A-Za-z_][A-Za-z0-9_]*", text))
                invalid = sorted(tokens - REPLY_ALLOWED_TOKENS)
                if invalid:
                    raise AssertionError(
                        f"{block.source}:{line}: reply {rule.name!r} uses unavailable "
                        f"tokens {invalid}; replies only know $self, $other and $map"
                    )

                normalized = re.sub(r"[^a-z0-9$]+", " ", text.lower()).strip()
                previous = normalized_reply_lines.get(normalized)
                if previous is not None:
                    previous_owner, previous_rule, previous_line = previous
                    raise AssertionError(
                        f"{block.source}:{line}: {block.name}/{rule.name} repeats the "
                        f"reply at {previous_owner}/{previous_rule}:{previous_line}"
                    )
                normalized_reply_lines[normalized] = (block.name, rule.name, line)

        missing_replies = sorted(set(REPLY_CATEGORIES) - set(replies_by_name))
        unknown_replies = sorted(set(replies_by_name) - set(REPLY_CATEGORIES))
        if missing_replies or unknown_replies:
            raise AssertionError(
                f"{block.source}: reply categories differ from the shipped contract; "
                f"missing {missing_replies}, unknown {unknown_replies}"
            )

        direct = replies_by_name["direct"]
        if direct.addressed != "required":
            raise AssertionError(
                f"{block.source}:{direct.line}: direct reply must require addressing"
            )
        owner_words = normalize_reply_phrase(block.name)
        if owner_words not in {
            normalize_reply_phrase(trigger) for trigger, _ in direct.triggers
        }:
            raise AssertionError(
                f"{block.source}:{direct.line}: direct reply does not trigger on its "
                f"owner name {block.name!r}"
            )

    missing_chats = sorted(name for name in seen if name not in chats_by_owner)
    if missing_chats:
        raise AssertionError(f"Characters without a dedicated .chat file: {missing_chats}")

    # Every style has to be worn by someone, or the archetype is unreachable.
    worn = {block.inherit.lower() for block in characters}
    unworn = sorted(name for name in styles if name not in worn)
    if unworn:
        raise AssertionError(f"No character uses the {unworn} style; the archetype is unreachable")


def chat_event_words(manager: Path, count: int) -> list[str]:
    """The event key words a content file may use.

    The documentation is the readable list, the enum fixes how many there are,
    and the game has to answer to every word in it - a word a character file may
    write that ChatEventForName does not know is a chat block that never fires.
    """

    doc = read(DOC)
    section = doc[doc.index("\n## Chat") :]
    section = section[: section.index("\n## ", 1)]
    listed = re.search(r"The events are:(.*?)\.\s", section, re.DOTALL)
    if listed is None:
        raise AssertionError("docs/dev/mp-bots.md no longer lists the chat events")
    words = re.findall(r"`(\w+)`", listed.group(1))
    if len(words) != count:
        raise AssertionError(
            f"docs/dev/mp-bots.md lists {len(words)} chat events but rvBotChatEvent declares {count}"
        )

    if not manager.is_file():
        raise AssertionError(f"{rel(manager)} does not exist")
    source = read(manager)
    unknown = [word for word in words if f'"{word}"' not in source]
    if unknown:
        raise AssertionError(
            f"BotCharacter.cpp knows no chat event word for {unknown}, so a character file "
            "using one would parse and then never say anything"
        )
    return words


def check_block(block: Block, fields: set[str], constants: dict[str, int]) -> None:
    unknown = sorted({name for name in block.traits if name not in fields})
    if unknown:
        raise AssertionError(
            f"{block.source}: {block.kind} {block.name!r} sets {unknown}, which are not "
            "fields of botTraits_t and would be warned away at load"
        )

    for weapon in block.weapons:
        if weapon not in KNOWN_WEAPONS:
            raise AssertionError(
                f"{block.source}: {block.kind} {block.name!r} has an opinion about "
                f"{weapon!r}, which is not a multiplayer weapon class"
            )
    if len(set(block.weapons)) > constants["BOT_MAX_WEAPON_BIAS"]:
        raise AssertionError(
            f"{block.source}: {len(set(block.weapons))} weapon biases exceeds "
            f"BOT_MAX_WEAPON_BIAS ({constants['BOT_MAX_WEAPON_BIAS']})"
        )

    if len(block.skill_blocks) != len(set(block.skill_blocks)):
        raise AssertionError(
            f"{block.source}: {block.kind} {block.name!r} has two skill blocks for one level"
        )

    limit = constants["BOT_CHAT_MAX_LEN"]
    for event, lines in block.chat.items():
        if not lines:
            raise AssertionError(f"{block.source}: chat {event} declares no lines")
        for text, line in lines:
            if text.startswith("#"):
                raise AssertionError(
                    f"{block.source}:{line}: chat lines may not start with '#'; the broadcast "
                    "runs them through GetLocalizedString and would substitute it away"
                )
            if len(text) > limit:
                raise AssertionError(
                    f"{block.source}:{line}: chat line is {len(text)} characters, over the "
                    f"BOT_CHAT_MAX_LEN budget of {limit}"
                )
        if len({text for text, _ in lines}) != len(lines):
            raise AssertionError(
                f"{block.source}: chat {event} repeats a line, which wastes a slot in the "
                "no-immediate-repeat rotation"
            )

    for rule in block.replies:
        if not rule.lines:
            raise AssertionError(
                f"{block.source}:{rule.line}: reply {rule.name!r} declares no lines"
            )
        for text, line in rule.lines:
            if text.startswith("#"):
                raise AssertionError(
                    f"{block.source}:{line}: reply lines may not start with '#'; the "
                    "broadcast path would interpret one as a localisation key"
                )
            if len(text) > limit:
                raise AssertionError(
                    f"{block.source}:{line}: reply line is {len(text)} characters, over "
                    f"the BOT_CHAT_MAX_LEN budget of {limit}"
                )
        if len({text for text, _ in rule.lines}) != len(rule.lines):
            raise AssertionError(
                f"{block.source}:{rule.line}: reply {rule.name!r} repeats an exact line"
            )


def validate_cvars_and_commands() -> None:
    doc = read(DOC)

    cvar_table = doc[doc.index("\n## Cvars") :]
    cvar_table = cvar_table[: cvar_table.index("\n## ", 1)]
    documented = re.findall(r"^\|\s*`(bot_\w+)`\s*\|\s*`([^`]*)`\s*\|", cvar_table, re.MULTILINE)
    if len(documented) < 14:
        raise AssertionError(
            f"docs/dev/mp-bots.md documents {len(documented)} bot cvars; the block has fourteen"
        )

    command_table = doc[doc.index("\n## Commands") :]
    command_table = command_table[: command_table.index("\n## ", 1)]
    commands = {row.split()[0] for row in re.findall(r"^\|\s*`([^`]+)`", command_table, re.MULTILINE)}
    for expected in ("addbot", "botcharacters", "botreload", "botlist"):
        if expected not in commands:
            raise AssertionError(f"docs/dev/mp-bots.md does not document the {expected!r} command")

    mp = GAME_LIBS_ROOT / "src" / "mpgame"
    if not mp.is_dir():
        return

    declared = read(mp / "gamesys" / "SysCvar.cpp")
    exported = read(mp / "gamesys" / "SysCvar.h")

    for name, default in documented:
        match = re.search(rf'^idCVar {name}\(\s*"{name}",\s*"([^"]*)",\s*([^,]+),', declared, re.MULTILINE)
        if match is None:
            raise AssertionError(f"{name} is documented but not defined in mpgame SysCvar.cpp")
        require(exported, f"extern idCVar {name};", "mpgame SysCvar.h")
        documented_default = default.strip('"')
        if match.group(1) != documented_default:
            raise AssertionError(
                f"{name} defaults to {match.group(1)!r} but the documentation says "
                f"{documented_default!r}"
            )

    # A command line +set never reaches a CVAR_GAME cvar, because the game module
    # registers it after the engine has parsed the command line.  Anything an
    # operator or a test harness has to set therefore has to be archived.
    for name in (
        "bot_enable",
        "bot_minPlayers",
        "bot_skill",
        "bot_skillVariance",
        "bot_characters",
        "bot_chat",
        "bot_chatDelay",
        "bot_chatCPM",
    ):
        line = re.search(rf"^idCVar {name}\(.*$", declared, re.MULTILINE)
        if line is None or "CVAR_ARCHIVE" not in line.group(0):
            raise AssertionError(
                f"{name} is not CVAR_ARCHIVE, so a server config cannot set it and a "
                "command line +set will not reach it either"
            )

    # And the two tuning knobs deliberately are not: an archived
    # bot_forceCharacter would field a whole roster of clones.
    for name in ("bot_forceCharacter", "bot_debugAim"):
        line = re.search(rf"^idCVar {name}\(.*$", declared, re.MULTILINE)
        if line is None:
            raise AssertionError(f"{name} is not defined in mpgame SysCvar.cpp")
        if "CVAR_ARCHIVE" in line.group(0):
            raise AssertionError(f"{name} is archived; it is a debugging knob and must not be")

    source = read(mp / "gamesys" / "SysCmds.cpp")
    for command in sorted(commands):
        require(source, f'"{command}",', "idGameLocal::InitConsoleCommands")

    # addbot grew an optional per-bot skill override; the handler has to read it.
    handler = source[source.index("void Cmd_AddBot_f") :][:1200]
    require(handler, "args.Argv( 2 )", "Cmd_AddBot_f skill argument")
    require(handler, "BOT_SKILL_LEVELS", "Cmd_AddBot_f skill argument")


def validate_chat_path() -> None:
    mp = GAME_LIBS_ROOT / "src" / "mpgame"
    if not mp.is_dir():
        return

    # Comments are stripped throughout: Bot.cpp explains at length why it does
    # NOT use the hitscan flag, and a check that read the prose would fire on
    # the code that got it right.
    sources = {
        path.name: strip_comments(read(path))
        for path in (mp / "bots" / "Bot.cpp", mp / "bots" / "BotCharacter.cpp")
        if path.is_file()
    }
    callers = {name: text for name, text in sources.items() if "ProcessChatMessage(" in text}
    if not callers:
        raise AssertionError(
            "Nothing under bots/ calls ProcessChatMessage; bot chat has to go out through the "
            "same server-side call a human's say uses, or every bot speaks as the host"
        )

    for name, text in callers.items():
        at = text.index("ProcessChatMessage(")
        window = text[max(0, at - 2500) : at]
        require(window, "AllowChat(", f"{name} before ProcessChatMessage")
        require(window, "IsTeamGame()", f"{name} before ProcessChatMessage")

    bot = sources["Bot.cpp"]

    # A kicked bot has to give its identity back, in Shutdown specifically, or
    # the roster drains and every later bot falls through to "any character at
    # all".  Releasing it somewhere else does not cover the kick.
    teardown = bot[bot.index("void rvBot::Shutdown") :][:1500]
    require(teardown, "ReleaseCharacter(", "rvBot::Shutdown")
    require(teardown, "isChatting = false", "rvBot::Shutdown typing-icon cleanup")

    # The gauntlet and the lightning gun set neither def_projectile nor
    # def_hitscan, so attackHitscan is false for both even though they are
    # instant-hit.  A bot that leads whenever !attackHitscan misses everything.
    if "attackHitscan" in bot:
        raise AssertionError(
            "Bot.cpp tests wfl.attackHitscan; it is false for the gauntlet and the lightning "
            "gun, so the projectile test has to be def_projectile instead"
        )
    require(bot, '"def_projectile"', "rvBot aim lead")

    # Every multiplayer projectile is retuned by a _mp decl, which only
    # FindEntityDef's fallback picks up.  A hardcoded speed leads by the wrong
    # amount on every shot.
    require(bot, "FindEntityDefDict(", "rvBot projectile speed lookup")

    # Every tactical personality field must reach behavior code.  The parser
    # accepting a content key is not enough: an unconsumed float would let the
    # roster validate while every character still played identically.
    for trait in TACTICAL_TRAITS:
        require(bot, f"traits.{trait}", f"rvBot tactical trait {trait}")

    # The manager reads the same source files a warning would name, and the
    # dead Quake 3 prototypes sit in the same tree.
    require(sources["BotCharacter.cpp"], "ReleaseCharacter", "rvBotCharacterManager")


def validate_reply_runtime() -> None:
    """Guard the server-only conversational-reply path and its loop brakes."""

    mp = GAME_LIBS_ROOT / "src" / "mpgame"
    if not mp.is_dir():
        return

    bot = strip_comments(read(mp / "bots" / "Bot.cpp"))
    bot_header = strip_comments(read(mp / "bots" / "Bot.h"))
    character = strip_comments(read(mp / "bots" / "BotCharacter.cpp"))
    multiplayer = strip_comments(read(mp / "MultiplayerGame.cpp"))
    multiplayer_header = strip_comments(read(mp / "MultiplayerGame.h"))
    commands = strip_comments(read(mp / "gamesys" / "SysCmds.cpp"))
    network = strip_comments(read(mp / "Game_network.cpp"))

    normalize_at = character.index("void rvBotCharacterManager::NormalizeReplyText")
    phrase_at = character.index(
        "bool rvBotCharacterManager::ReplyPhraseMatches", normalize_at
    )
    normalize = character[normalize_at:phrase_at]
    for needle in (
        "RemoveEscapes( S_ESCAPE_ALL )",
        "CharIsAlpha(",
        "CharIsNumeric(",
        "ToLower(",
    ):
        require(normalize, needle, "rvBotCharacterManager::NormalizeReplyText")

    phrase_end = character.index(
        "bool rvBotCharacterManager::ReplyNameMatches", phrase_at
    )
    phrase = character[phrase_at:phrase_end]
    require(phrase, "leftBoundary", "ReplyPhraseMatches whole-word left boundary")
    require(phrase, "rightBoundary", "ReplyPhraseMatches whole-word right boundary")

    require(
        multiplayer_header,
        "bool triggerBotReplies",
        "ProcessChatMessage reply provenance parameter",
    )

    process_at = multiplayer.index("void idMultiplayerGame::ProcessChatMessage")
    process = braced_body(multiplayer, process_at, "idMultiplayerGame::ProcessChatMessage")
    require(process, "triggerBotReplies", "idMultiplayerGame::ProcessChatMessage")
    require(process, "botManager.OnChatMessage(", "idMultiplayerGame::ProcessChatMessage")
    require(
        process,
        "triggerBotReplies && clientNum >= 0 && send_to != 1",
        "ProcessChatMessage typed/non-system/non-spectator reply gate",
    )
    require(
        process,
        "common->GetLocalizedString( text )",
        "ProcessChatMessage visible reply-match text",
    )
    require_order(
        process,
        "suffixed_name.Length() + prefixed_text.Length()",
        "botManager.OnChatMessage(",
        "ProcessChatMessage validates before offering a reply",
    )
    require_order(
        process,
        "for ( i = 0; i < gameLocal.numClients; i++ )",
        "botManager.OnChatMessage(",
        "ProcessChatMessage fans out accepted chat before offering a reply",
    )

    # Typed say is eligible.  Voice-command text is not: its canned phrases
    # should not make a bot answer the voice menu, and both voice branches need
    # the false provenance even when one has no sound shader.
    say_at = commands.index("static void Cmd_Say(")
    say = braced_body(commands, say_at, "Cmd_Say")
    require_regex(
        say,
        r"ProcessChatMessage\s*\([^;]*NULL\s*,\s*true\s*\)",
        "Cmd_Say typed-chat provenance",
    )

    remote_at = network.index("void idGameLocal::ServerProcessReliableMessage")
    remote = braced_body(network, remote_at, "idGameLocal::ServerProcessReliableMessage")
    require_regex(
        remote,
        r"ProcessChatMessage\s*\(\s*clientNum\s*,[^;]*NULL\s*,\s*true\s*\)",
        "remote client typed-chat provenance",
    )

    voice_at = multiplayer.index("void idMultiplayerGame::ProcessVoiceChat")
    voice = braced_body(multiplayer, voice_at, "idMultiplayerGame::ProcessVoiceChat")
    voice_calls = re.findall(r"ProcessChatMessage\s*\((.*?)\)\s*;", voice, re.DOTALL)
    if len(voice_calls) < 2:
        raise AssertionError(
            "ProcessVoiceChat no longer has both sound and no-sound chat branches"
        )
    for call in voice_calls:
        if not re.search(r",\s*false\s*$", call):
            raise AssertionError(
                "ProcessVoiceChat calls ProcessChatMessage without false reply provenance"
            )

    update_at = bot.index("void rvBot::UpdateChat")
    update = braced_body(bot, update_at, "rvBot::UpdateChat")
    require(update, "isChatting =", "rvBot::UpdateChat typing icon")
    require(update, "!chatPending.IsEmpty()", "rvBot::UpdateChat typing icon pending state")
    require(update, "gameLocal.time < chatSendTime", "rvBot::UpdateChat typing icon deadline")
    require(update, "chatPendingIsReply", "rvBot::UpdateChat reply provenance")
    require(update, "!wasReply", "rvBot::UpdateChat recursion suppression")
    require_regex(
        update,
        r"ProcessChatMessage\s*\([^;]*!\s*wasReply\s*\)",
        "rvBot::UpdateChat recursion suppression",
    )

    queue_event_at = bot.index("void rvBot::QueueChat")
    queue_reply_at = bot.index("bool rvBot::TryQueueReply", queue_event_at)
    queue_event = bot[queue_event_at:queue_reply_at]
    require(
        queue_event,
        "chatPendingIsReply = false",
        "rvBot::QueueChat event-chat provenance",
    )
    require(queue_event, "BotChatSendTime(", "rvBot::QueueChat CPM delay")

    queue_at = bot.index("bool rvBot::TryQueueReply")
    update_signature_at = bot.index("void rvBot::UpdateChat", queue_at)
    queue = bot[queue_at:update_signature_at]
    require(
        queue,
        "!chatPending.IsEmpty()",
        "rvBot::TryQueueReply pending-line preservation",
    )
    require(queue, "ReplyLine(", "rvBot::TryQueueReply content selection")
    require(queue, "AllowChat(", "rvBot::TryQueueReply existing flood throttle")
    require(queue, "chatPendingIsReply = true", "rvBot::TryQueueReply provenance stamp")
    require(queue, "BotChatSendTime(", "rvBot::TryQueueReply CPM delay")
    require_order(
        queue,
        "ReplyLine(",
        "AllowChat(",
        "rvBot::TryQueueReply usable-line-before-throttle ordering",
    )

    delay_at = bot.index("static int BotChatSendTime")
    display_name_at = bot.index("static idStr BotReadableName", delay_at)
    delay = bot[delay_at:display_name_at]
    for needle in (
        "LengthWithoutEscapes()",
        "bot_chatCPM.GetInteger()",
        "60000.0f",
        "traits.chatDelayScale",
        "BOT_CHAT_MIN_DELAY_MSEC",
        "BOT_CHAT_MAX_DELAY_MSEC",
    ):
        require(delay if needle != "traits.chatDelayScale" else queue_event,
                needle, "rvBot length-based CPM chat delay")

    icon_manager = strip_comments(read(mp / "IconManager.cpp"))
    chat_icons_at = icon_manager.index("void rvIconManager::UpdateChatIcons")
    chat_icons = braced_body(icon_manager, chat_icons_at, "rvIconManager::UpdateChatIcons")
    require(chat_icons, "player->isChatting", "Quake 4 stock chat icon state")
    require(chat_icons, '"mtr_icon_chatting"', "Quake 4 stock chat icon material")

    on_chat_at = bot.index("void rvBotManager::OnChatMessage")
    num_bots_at = bot.index("int rvBotManager::NumBots", on_chat_at)
    on_chat = bot[on_chat_at:num_bots_at]
    for needle, context in (
        ("BOT_REPLY_SOURCE_THROTTLE_MSEC", "source cooldown"),
        ("nextReplySourceTime[sourceClientNum]", "per-source cooldown"),
        ("i == sourceClientNum", "speaker exclusion"),
        ("candidatePlayer->spectating", "spectator exclusion"),
        ("candidatePlayer->team != sourcePlayer->team", "team visibility filter"),
        ("candidatePlayer->IsPlayerMuted( sourcePlayer )", "mute visibility filter"),
        ("addressedCandidates", "addressed responder preference"),
        ("generalCandidates", "unaddressed responder pool"),
        ("sourceIsBot = IsBot( sourceClientNum )", "player/bot source classification"),
        ("NormalizeReplyText( visibleText", "live-message normalization"),
        ('gameLocal.userInfo[sourceClientNum].GetString( "ui_name"', "trusted source name"),
    ):
        require(on_chat, needle, f"rvBotManager::OnChatMessage {context}")

    require(
        on_chat,
        "addressedCandidates.Num() ? &addressedCandidates : &generalCandidates",
        "rvBotManager::OnChatMessage addressed preference",
    )
    if on_chat.count(".TryQueueReply(") != 1:
        raise AssertionError(
            "rvBotManager::OnChatMessage must call TryQueueReply exactly once, so one "
            "incoming line cannot make several bots answer"
        )
    require(on_chat, "RandomInt( candidates->Num() )", "one random reply responder")
    require_order(
        on_chat,
        ".TryQueueReply(",
        "nextReplySourceTime[sourceClientNum] =",
        "rvBotManager::OnChatMessage cooldown only after a queued reply",
    )

    for needle in (
        "TryQueueReply(",
        "OnChatMessage(",
        "nextReplySourceTime[MAX_CLIENTS]",
        "chatPendingIsReply",
    ):
        require(bot_header, needle, "Bot.h reply state/API")


def main() -> int:
    try:
        validate_reply_matcher_vectors()
        validate_wiring()
        validate_manager()
        validate_content()
        validate_cvars_and_commands()
        validate_chat_path()
        validate_reply_runtime()
    except AssertionError as error:
        print(f"mp_bot_characters: FAILED - {error}")
        return 1

    print("mp_bot_characters: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
