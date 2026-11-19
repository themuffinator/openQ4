#!/usr/bin/env python3
"""Regression checks for text encoding and the code-point font path.

openQ4 text is UTF-8 end to end.  String tables are authored in it, the loader
normalises a legacy 8-bit table *up* to it (idLangDict::Load), and the GUI draws
by Unicode code point rather than by byte, so a language is limited by what the
fonts have art for rather than by what one 8-bit codepage can express.

Four things therefore have to stay true:

  * every repo-authored ``.lang`` file is valid UTF-8, and every code point in it
    is one the engine actually builds a glyph for - anything else draws as a
    question mark;
  * the codepage tables in the engine match the real Windows codepages.  They
    are no longer the rendering path, but they still decode retail tables and
    still index the retail bitmap atlases, and a wrong entry is not a missing
    glyph but a confidently wrong one - 0xB9 is superscript one in CP1252,
    a-ogonek in CP1250 and a soft hyphen in CP1251;
  * the code-point glyph lookup, the extended pages that back it, and the
    UTF-8 iteration in the text layer all stay in place;
  * a language the 256-glyph bitmap atlases cannot draw at all does not silently
    honour r_useTrueTypeFonts 0.
"""

import re
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
STRINGS_DIR = ROOT / "content" / "baseoq4" / "pak0" / "strings"
FONTS_DIR = ROOT / "content" / "baseoq4" / "pak0" / "fonts"

# sys_lang values whose legacy 8-bit tables and retail atlases are not
# Windows-1252.  Keep in step with LangDict_CodePageForLanguage.
CODEPAGE_LANGUAGES = {
    "polish": "cp1250",
    "czech": "cp1250",
    "hungarian": "cp1250",
    "russian": "cp1251",
    "ukrainian": "cp1251",
    "turkish": "cp1254",
}

# Of those, the ones the retail bitmap atlases draw wrongly or not at all, so
# LangDict_LanguageNeedsScalableFonts must override r_useTrueTypeFonts 0.  Only
# Polish had a retail release with atlases for its own codepage.
SCALABLE_ONLY_LANGUAGES = ("czech", "hungarian", "russian", "turkish", "ukrainian")

# Languages whose five non-dialogue tables (code, guis, maps, mappack, openq4)
# translate every English entry, with the endonym the language chooser shows
# for each.  Campaign dialogue (lips) falls back to English or to retail media.
FULL_TABLE_LANGUAGES = {
    "german": "Deutsch",
    "brazilian": "Português (Brasil)",
    "czech": "Čeština",
    "hungarian": "Magyar",
    "turkish": "Türkçe",
    "ukrainian": "Українська",
    "russian": "Русский",
}

# Letters each of those languages is not written without.  They must turn up
# in its tables - a table that never uses them is not in that language, or lost
# its encoding - and every face that draws text must have art for them.
SIGNATURE_LETTERS = {
    "german": "ÄÖÜäöüß",
    "brazilian": "ãçõáéíóúê",
    "czech": "čěřšžůýá",
    "hungarian": "őűöüáéí",
    "turkish": "ğışçöüİ",
    "ukrainian": "єіїщЄІ",
    "russian": "ыэъёЫЭ",
}

# The chooser appends languages, so an index never moves once shipped: the
# Spanish/French layout fixes in mainmenu.gui and every player's archived
# sys_lang depend on it.
SHIPPED_LANGUAGE_ORDER = (
    "english", "spanish", "french", "italian", "polish", "russian", "german",
    "brazilian", "czech", "hungarian", "turkish", "ukrainian",
)

# Unicode blocks above U+00FF the font rasteriser always builds pages for.
# Keep in step with Q4_TTF_UNIVERSAL_RANGES in src/renderer/tr_fontTTF.cpp.
# The Latin extensions are here rather than per-language because Windows-1252
# itself reaches into them - the shipped French tables spell "coeur" with a real
# o-e ligature - so no language gets to opt out.
UNIVERSAL_RANGES = (
    (0x0100, 0x017F),   # Latin Extended-A
    (0x0180, 0x024F),   # Latin Extended-B
    (0x02B0, 0x02FF),   # Spacing Modifier Letters
    (0x2000, 0x20FF),   # General Punctuation, Currency Symbols
    (0x2100, 0x21FF),   # Letterlike Symbols, Arrows
)

# Non-Latin scripts a language additionally needs.  Keep in step with
# LangDict_ExtendedRangesForLanguage.
LANGUAGE_RANGES = {
    "cp1251": ((0x0400, 0x04FF),),
}

# 'strogg' is a decorative alien face, Latin-only by design. The retained Strogg
# pause draws every label's rune copy in it, through the host's fold of the
# code points it lacks (RuneScalar in src/ui/RetainedUI.cpp), and
# ui_retained_gate.py checks that fold against this face for every shipped
# string, so it needs no coverage of its own here.
LATIN_ONLY_FACES = {"strogg"}

# Codes that land on a .notdef cell in every stock Quake 4 font.  The engine
# folds these to ASCII rather than drawing nothing; U+00A0 in particular has a
# zero advance, so leaving it alone would delete the word gap entirely.  All
# three Windows codepages put this punctuation at the same byte values, so one
# fold table serves every one of them.
FOLDED_BYTES = {0x82, 0x84, 0x85, 0x91, 0x92, 0x93, 0x94, 0x96, 0x97, 0xA0}


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def codepage_for_lang_file(path: Path) -> str:
    language = path.stem.split("_", 1)[0].lower()
    return CODEPAGE_LANGUAGES.get(language, "cp1252")


def parse_hex_table(source: str, name: str, expected_length: int) -> list:
    """Pull a `static const unsigned short NAME[N] = { ... };` table out of C++."""
    match = re.search(
        r"\b" + re.escape(name) + r"\s*\[\s*\d+\s*\]\s*=\s*\{(.*?)\}\s*;",
        source,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"Could not find the {name} table")
    values = [int(token, 16) for token in re.findall(r"0x([0-9A-Fa-f]{4})", match.group(1))]
    if len(values) != expected_length:
        raise AssertionError(
            f"{name} has {len(values)} entries, expected {expected_length}"
        )
    return values


def validate_codepage_table(source: str, name: str, codec: str, first_byte: int, count: int) -> None:
    """The engine's table must agree with the real Windows codepage, entry for entry."""
    values = parse_hex_table(source, name, count)
    for index, value in enumerate(values):
        byte = first_byte + index
        try:
            expected = ord(bytes([byte]).decode(codec))
        except UnicodeDecodeError:
            expected = 0  # unassigned slot; the engine marks these 0
        if value != expected:
            raise AssertionError(
                f"{name}[0x{byte:02X}] is 0x{value:04X}, but {codec} maps that byte to "
                f"0x{expected:04X}. A wrong entry draws a confidently wrong glyph."
            )


def font_code_points(path: Path) -> set:
    """The code points a .ttf covers, read straight out of its cmap.

    Deliberately dependency-free: this test guards the shipped content and has
    to run wherever CI does, without fontTools.  Formats 4 and 12 are the only
    two the generator emits.
    """
    data = path.read_bytes()
    if len(data) < 12:
        raise AssertionError(f"{path.name} is too short to be a font")

    num_tables = struct.unpack_from(">H", data, 4)[0]
    cmap_offset = None
    for i in range(num_tables):
        tag, _checksum, offset, _length = struct.unpack_from(">4sIII", data, 12 + i * 16)
        if tag == b"cmap":
            cmap_offset = offset
            break
    if cmap_offset is None:
        raise AssertionError(f"{path.name} has no cmap table")

    num_subtables = struct.unpack_from(">H", data, cmap_offset + 2)[0]
    covered = set()
    for i in range(num_subtables):
        _platform, _encoding, sub_offset = struct.unpack_from(">HHI", data, cmap_offset + 4 + i * 8)
        table = cmap_offset + sub_offset
        fmt = struct.unpack_from(">H", data, table)[0]

        if fmt == 4:
            seg_x2 = struct.unpack_from(">H", data, table + 6)[0]
            segments = seg_x2 // 2
            ends = table + 14
            starts = ends + seg_x2 + 2
            for seg in range(segments):
                end = struct.unpack_from(">H", data, ends + seg * 2)[0]
                start = struct.unpack_from(">H", data, starts + seg * 2)[0]
                if start > end or end == 0xFFFF and start == 0xFFFF:
                    continue
                covered.update(range(start, end + 1))
        elif fmt == 12:
            groups = struct.unpack_from(">I", data, table + 12)[0]
            for group in range(groups):
                start, end, _glyph = struct.unpack_from(">III", data, table + 16 + group * 12)
                covered.update(range(start, min(end, 0xFFFF) + 1))

    if not covered:
        raise AssertionError(f"{path.name}: no usable cmap subtable")
    return covered


def in_ranges(code_point: int, ranges) -> bool:
    return any(first <= code_point <= last for first, last in ranges)


def drawable_ranges(codepage: str):
    return UNIVERSAL_RANGES + LANGUAGE_RANGES.get(codepage, ())


def validate_lang_files() -> None:
    lang_files = sorted(STRINGS_DIR.glob("*.lang"))
    if not lang_files:
        raise AssertionError(f"No .lang files found under {STRINGS_DIR}")

    for path in lang_files:
        data = path.read_bytes()
        rel = path.relative_to(ROOT).as_posix()
        codepage = codepage_for_lang_file(path)

        if data.startswith(b"\xef\xbb\xbf"):
            raise AssertionError(
                f"{rel} starts with a UTF-8 BOM; idLangDict::Load tolerates it but "
                "the retail parser does not - save without a BOM"
            )

        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise AssertionError(
                f"{rel} is not valid UTF-8 at byte {exc.start} ({exc.reason}). "
                "Repo-authored string tables are UTF-8; only *retail* tables are "
                "8-bit, and those are normalised to UTF-8 as they load."
            ) from exc

        allowed = drawable_ranges(codepage)
        for index, char in enumerate(text):
            code_point = ord(char)
            # The base 256 glyph slots are Latin-1, so anything there is always
            # addressable whichever font path is active.
            if code_point <= 0xFF:
                continue
            if not in_ranges(code_point, allowed):
                raise AssertionError(
                    f"{rel}: U+{code_point:04X} ({char!r}) at character {index} is outside "
                    "every Unicode block the font rasteriser builds pages for, so it would "
                    "draw as a question mark. Either use a character in an existing block or "
                    "add the block to LangDict_ExtendedRangesForLanguage and to "
                    "LANGUAGE_RANGES in this test."
                )


def validate_font_coverage() -> None:
    """Every face that draws a string table must have art for what the tables
    actually say, plus the whole of any non-Latin script a language declares -
    text typed at runtime never passed through a table but still has to draw."""
    faces = sorted(FONTS_DIR.glob("*.ttf"))
    if not faces:
        raise AssertionError(f"No .ttf faces found under {FONTS_DIR}")

    required = set()
    for path in STRINGS_DIR.glob("*.lang"):
        for first, last in LANGUAGE_RANGES.get(codepage_for_lang_file(path), ()):
            required.update(range(first, last + 1))
        for char in path.read_text(encoding="utf-8"):
            if ord(char) > 0xFF:
                required.add(ord(char))
    if not required:
        return

    for path in faces:
        if path.stem in LATIN_ONLY_FACES:
            continue
        missing = sorted(required - font_code_points(path))
        if missing:
            raise AssertionError(
                f"{path.relative_to(ROOT).as_posix()} is missing {len(missing)} code points a "
                f"shipped string table needs, starting at U+{missing[0]:04X}. Rebuild the faces "
                "with tools/assets/fonts/build.py, or add the face to LATIN_ONLY_FACES if it is "
                "decorative and never draws a string table."
            )


def validate_transcode_contract() -> None:
    for relative_path in (
        "src/idlib/LangDict.cpp",
        # the game repo builds its own idlib; keep the two copies in lockstep
    ):
        source = read(relative_path)
        context = f"{relative_path} language-table normalisation"
        # The direction matters: converting the other way, UTF-8 down to a
        # codepage, is what capped the engine at one 8-bit page per language.
        require(source, "LangDict_ConvertCodePageToUtf8", context)
        if "LangDict_ConvertUtf8ToCodePage" in source:
            raise AssertionError(
                f"{relative_path} still transcodes UTF-8 down to an 8-bit codepage; "
                "string tables are kept in UTF-8 now and the font path is code-point keyed"
            )
        require(source, "LANGDICT_CP1252_HIGH", context)
        require(source, "LANGDICT_CP1250_HIGH", context)
        require(source, "LANGDICT_CP1251_HIGH", context)
        require(source, "LANGDICT_CP1254_HIGH", context)
        require(source, "LANGDICT_GLYPH_FOLD", context)
        require(source, "LangDict_DecodeUtf8", context)
        require(source, "LangDict_EncodeUtf8", context)
        require(source, "LangDict_NextCodePoint", context)
        require(source, "LangDict_ByteForCodePoint", context)
        require(source, "LangDict_CodePageForLanguage", context)
        require(source, "LangDict_UnicodeForByte", context)
        require(source, "LangDict_ExtendedRangesForLanguage", context)
        require(source, "LangDict_LanguageNeedsScalableFonts", context)
        require(
            source,
            "idStr transcoded;",
            f"{context} (buffer must outlive the lexer)",
        )
        require(
            source,
            "src.LoadMemory( parseText, parseLength, fileName );",
            f"{context} (lexer must parse the normalised text)",
        )

        # CP1252 only tabulates 0x80-0x9F because it agrees with Latin-1 above
        # that, which is what the engine relies on for the pass-through branch.
        validate_codepage_table(source, "LANGDICT_CP1252_HIGH", "cp1252", 0x80, 32)
        for byte in range(0xA0, 0x100):
            if ord(bytes([byte]).decode("cp1252")) != byte:
                raise AssertionError(
                    f"cp1252 does not map 0x{byte:02X} to itself, so LangDict's Latin-1 "
                    "pass-through above 0x9F is wrong"
                )
        # CP1250 and CP1251 diverge from Latin-1 across the whole upper half.
        validate_codepage_table(source, "LANGDICT_CP1250_HIGH", "cp1250", 0x80, 128)
        validate_codepage_table(source, "LANGDICT_CP1251_HIGH", "cp1251", 0x80, 128)
        validate_codepage_table(source, "LANGDICT_CP1254_HIGH", "cp1254", 0x80, 128)

        for language in CODEPAGE_LANGUAGES:
            require(source, f'"{language}"', f"{context} (codepage language list)")

        # The bitmap policy names its languages; a codepage language missing
        # from it would draw Windows-1252 art for its own letters.
        scalable = source[source.index("bool LangDict_LanguageNeedsScalableFonts(") :]
        scalable = scalable[: scalable.index("\n}\n")]
        for language in SCALABLE_ONLY_LANGUAGES:
            if CODEPAGE_LANGUAGES[language] != "cp1251":
                require(scalable, f'"{language}"', f"{context} (LangDict_LanguageNeedsScalableFonts)")
        if '"polish"' in scalable:
            raise AssertionError(
                f"{relative_path}: Polish had a retail release with its own Central "
                "European atlases; LangDict_LanguageNeedsScalableFonts must not force it"
            )

        declaration = source.index("idStr transcoded;")
        lexer = source.index("idLexer src(")
        if declaration > lexer:
            raise AssertionError(
                f"{relative_path}: 'idStr transcoded' must be declared before 'idLexer src' - "
                "idLexer::LoadMemory stores the pointer without copying, so the buffer has to "
                "be destroyed after the lexer"
            )

        # every folded code must be one the engine can actually produce
        for match in re.finditer(r"\{\s*0x([0-9A-Fa-f]{2}),\s*0x[0-9A-Fa-f]{4},\s*\"", source):
            code = int(match.group(1), 16)
            if code not in FOLDED_BYTES:
                raise AssertionError(
                    f"{relative_path}: unexpected glyph fold entry 0x{code:02X}; update "
                    "FOLDED_BYTES in this test if the fold table intentionally changed"
                )


def validate_code_point_font_path() -> None:
    """Text is UTF-8, so glyphs are found by code point and everything above
    U+00FF lives in the extended pages."""
    render_system = read("src/renderer/RenderSystem.h")
    for needle in (
        "R_GlyphForCodePoint",
        "GLYPH_INDEX_CODEPAGE",
        "GLYPH_INDEX_UNICODE",
        "fontGlyphPage_t",
        "extendedPages",
        "glyphIndexing",
    ):
        require(render_system, needle, "src/renderer/RenderSystem.h code-point glyph lookup")

    ttf = read("src/renderer/tr_fontTTF.cpp")
    for needle in (
        "R_TTFBuildExtendedPages",
        "R_TTFCollectExtendedCodePoints",
        "Q4_TTF_UNIVERSAL_RANGES",
        "LangDict_ExtendedRangesForLanguage",
        "GLYPH_INDEX_UNICODE",
    ):
        require(ttf, needle, "src/renderer/tr_fontTTF.cpp extended glyph pages")
    if "R_TTFCodepointForByte" in ttf:
        raise AssertionError(
            "src/renderer/tr_fontTTF.cpp still maps GUI atlas slots through the active "
            "codepage; the base slots are Latin-1 now and everything above U+00FF is an "
            "extended page, or one atlas can still only express one 8-bit page"
        )
    # The console sheet is deliberately still byte-indexed, and so still has to
    # be rasterised through the active codepage.
    require(
        ttf,
        "LangDict_UnicodeForByte",
        "R_BuildConsoleFontAtlas (the console sheet is a codepage, not Latin-1)",
    )

    bitmap = read("src/renderer/tr_font.cpp")
    require(
        bitmap,
        "GLYPH_INDEX_CODEPAGE",
        "src/renderer/tr_font.cpp (a .fontdat is art for one 8-bit codepage)",
    )

    device_context = read("src/ui/DeviceContext.cpp")
    for needle in (
        "openQ4_NextTextGlyph",
        "LangDict_NextCodePoint",
        "R_GlyphForCodePoint",
        "CharWidthForCodePoint",
        "openQ4_CodePointIsPrintable",
    ):
        require(device_context, needle, "src/ui/DeviceContext.cpp UTF-8 text iteration")
    if re.search(r"font->glyphs\[\s*\*s\s*\]", device_context):
        raise AssertionError(
            "src/ui/DeviceContext.cpp still indexes glyphs by raw byte; a multi-byte "
            "character would draw as two wrong glyphs"
        )

    console = read("src/framework/Console.cpp")
    for needle in ("Con_ConsoleCellForCodePoint", "LangDict_NextCodePoint"):
        require(console, needle, "src/framework/Console.cpp UTF-8 console text")


def validate_bitmap_font_policy() -> None:
    """A language the retail atlases cannot draw must not honour
    r_useTrueTypeFonts 0 - it would produce a menu of question marks with
    nothing to explain it."""
    ttf = read("src/renderer/tr_fontTTF.cpp")
    require(ttf, "R_UseScalableFonts", "src/renderer/tr_fontTTF.cpp bitmap-font policy")
    require(
        ttf,
        "LangDict_LanguageNeedsScalableFonts",
        "R_UseScalableFonts (the language has to be able to override the cvar)",
    )
    for function in ("bool R_RegisterTrueTypeFont(", "bool R_BuildConsoleFontAtlas("):
        start = ttf.index(function)
        body = ttf[start : start + 400]
        if "R_UseScalableFonts()" not in body:
            raise AssertionError(
                f"src/renderer/tr_fontTTF.cpp: {function.strip()} reads r_useTrueTypeFonts "
                "directly; it must go through R_UseScalableFonts so a Cyrillic language "
                "cannot end up on the bitmap atlases"
            )


def validate_codepage_selection() -> None:
    """The codepage still has to be chosen before any table is read: it decides
    how a legacy 8-bit table is decoded, and which cells the console sheet gets."""
    common = read("src/framework/Common.cpp")
    require(
        common,
        "LangDict_SetActiveCodePage( LangDict_CodePageForLanguage( langName.c_str() ) )",
        "idCommonLocal::InitLanguageDict (codepage must follow sys_lang)",
    )
    init = common[common.index("void idCommonLocal::InitLanguageDict(") :]
    init = init[: init.index("\n}\n")]
    select = init.index("LangDict_SetActiveCodePage")
    # Tables load through Common_LoadLanguageFileLayers, which layers a mod's
    # copy over the copies it shadows.
    first_load = init.index("Common_LoadLanguageFileLayers( languageDict,")
    if select > first_load:
        raise AssertionError(
            "src/framework/Common.cpp: the active codepage must be selected before the "
            "first string table is loaded - one dictionary cannot hold two codepages"
        )
    if "languageDict.Load(" in init:
        raise AssertionError(
            "src/framework/Common.cpp: InitLanguageDict must load tables through "
            "Common_LoadLanguageFileLayers, or a mod's stale copy hides newer strings"
        )

    # The renderer is a module with its own copy of idlib, so the codepage the
    # engine selects does not reach the glyph rasteriser on its own.
    font = read("src/renderer/tr_font.cpp")
    require(font, "void R_SyncFontCodePage( void )", "renderer-module codepage sync")
    require(
        font,
        'LangDict_CodePageForLanguage( cvarSystem->GetCVarString( "sys_lang" ) )',
        "renderer-module codepage sync (must derive from the shared cvar)",
    )
    register = font[font.index("bool idRenderSystemLocal::RegisterFont") :]
    register = register[: register.index("\n}\n")]
    if "R_SyncFontCodePage();" not in register:
        raise AssertionError(
            "src/renderer/tr_font.cpp: RegisterFont must sync the module's codepage "
            "before any atlas is rasterised"
        )
    if register.index("R_SyncFontCodePage();") > register.index("LangDict_GetCodePageGeneration()"):
        raise AssertionError(
            "src/renderer/tr_font.cpp: the codepage sync must run before the generation "
            "is sampled, or the console atlas rebuild misses the language change"
        )


def validate_language_menu() -> None:
    """Every sys_lang the language chooser offers needs string tables to load."""
    game_gui = read("content/baseoq4/pak0/guis/menu/settings/game.gui")
    match = re.search(r'values\s+"([a-z;]+)"', game_gui)
    if match is None:
        raise AssertionError(
            "content/baseoq4/pak0/guis/menu/settings/game.gui: could not find the "
            "language chooser 'values' list"
        )
    offered = match.group(1).split(";")

    for language in offered:
        if not sorted(STRINGS_DIR.glob(f"{language}_*.lang")):
            raise AssertionError(
                f"The language menu offers '{language}' but no {language}_*.lang tables "
                f"ship in {STRINGS_DIR.relative_to(ROOT).as_posix()}; selecting it would "
                "fall back to English"
            )

    # Common_AppendLanguagesWithStringTables only admits names the engine
    # knows, so tables for a language missing from sysLanguageNames are never
    # offered to the resolver and the language silently reverts to English.
    # The OS-locale maps are what pick it on a player's first run.
    sys_local = read("src/sys/sys_local.cpp")
    names = sys_local[sys_local.index("sysLanguageNames[] = {") :]
    known = set(re.findall(r'"([a-z]+)"', names[: names.index("};")]))
    common = read("src/framework/Common.cpp")
    locale_map = common[common.index("static const char *Common_MapLocaleLanguageCode(") :]
    locale_map = locale_map[: locale_map.index("\n}\n")]
    windows_map = common[common.index("static const char *Common_MapWindowsPrimaryLanguage(") :]
    windows_map = windows_map[: windows_map.index("\n}\n")]
    for language in offered:
        if language not in known:
            raise AssertionError(
                f"The language menu offers '{language}' but src/sys/sys_local.cpp "
                "sysLanguageNames does not list it, so its string tables are ignored"
            )
        if f'"{language}" }}' not in locale_map or f'return "{language}";' not in windows_map:
            raise AssertionError(
                f"src/framework/Common.cpp: '{language}' needs an OS-locale mapping in both "
                "Common_MapLocaleLanguageCode and Common_MapWindowsPrimaryLanguage, or a "
                "first run never selects it"
            )

    # choiceDef lists are positional, so the names and the values must agree in
    # every language - a short list silently mismatches every entry after it.
    lists = {}
    for path in sorted(STRINGS_DIR.glob("*.lang")):
        names = re.search(r'"#str_229908"\s+"([^"]+)"', path.read_text(encoding="utf-8"))
        if names is None:
            continue
        if len(names.group(1).split(";")) != len(offered):
            raise AssertionError(
                f"{path.relative_to(ROOT).as_posix()}: #str_229908 lists "
                f"{len(names.group(1).split(';'))} language names but the chooser offers "
                f"{len(offered)} values; a choiceDef silently mismatches when they disagree"
            )
        lists.setdefault(path.stem.split("_", 1)[0], set()).add(names.group(1))
    # Both the guis and the openq4 table of a language define the list, and
    # whichever loads last wins; they must not disagree.
    for language, variants in sorted(lists.items()):
        if len(variants) != 1:
            raise AssertionError(f"{language}: the guis and openq4 tables carry different #str_229908 lists")


def validate_translated_tables() -> None:
    """Every full-table language: complete, positionally safe and drawable.

    Catches English fallbacks, unsafe printf arguments, broken choice lists
    and a chooser whose indices moved."""
    entry = re.compile(r'^\s*"(#str_\w+)"\s+"((?:\\.|[^"\\])*)"', re.MULTILINE)
    # No space flag: "100% daily value" is prose, not a "% d" conversion.
    formats = re.compile(r'%(?:[-+0#]*\d*(?:\.\d+)?(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcsp]|%)')
    # idStr::IsEscape: ^0-^9, ^:, ^-, ^+ and ^r are two characters, ^nX three,
    # ^cRGB and the ^iXYZ icons five.
    escapes = re.compile(r'\^(?:[cCiI][^\0]{3}|[nN][^\0]|[0-9:+\-rR])')
    english = {}
    tables = {language: {} for language in FULL_TABLE_LANGUAGES}
    defined = {language: {} for language in FULL_TABLE_LANGUAGES}
    for category in ("code", "guis", "maps", "mappack", "openq4"):
        source = entry.findall((STRINGS_DIR / f"english_{category}.lang").read_text(encoding="utf-8"))
        source_keys = [key for key, _ in source]
        english.update(source)
        for language in FULL_TABLE_LANGUAGES:
            target_path = STRINGS_DIR / f"{language}_{category}.lang"
            target = entry.findall(target_path.read_text(encoding="utf-8"))
            target_keys = [key for key, _ in target]
            assert source_keys and source_keys == target_keys, f"{target_path.name}: English key/order mismatch"
            assert len(target_keys) == len(set(target_keys)), f"{target_path.name}: duplicate keys"
            for (key, original), (_, translated) in zip(source, target):
                label = f"{target_path.name}: {key}"
                assert bool(original.strip()) == bool(translated.strip()), f"{label}: empty translation"
                assert formats.findall(original) == formats.findall(translated), f"{label}: printf arguments changed"
                assert escapes.findall(original) == escapes.findall(translated), f"{label}: GUI escapes changed"
                assert re.findall(r'\\[nrt]', original) == re.findall(r'\\[nrt]', translated), f"{label}: layout escapes changed"
                # Choice lists are positional wherever they are consumed - .gui
                # choiceDefs, retained settings, code - so every value keeps the
                # English count; a prose semicolon stays punctuation.
                if key != "#str_229908":
                    assert original.count(";") == translated.count(";"), f"{label}: semicolon count changed"
                # A key in two tables shows only the later table's text, so a
                # second translation is silently dead; the two must agree.
                earlier = defined[language].setdefault(key, translated)
                assert earlier == translated, f"{label}: differs from the same key in an earlier {language} table"
            tables[language].update(target)

    # The chooser appends: Spanish/French layout fixes and every archived
    # sys_lang depend on the existing indices.
    game_gui = read("content/baseoq4/pak0/guis/menu/settings/game.gui")
    chooser = game_gui[game_gui.index("choiceDef set_game_language_value"):]
    values = re.search(r'values\s+"([a-z;]+)"', chooser).group(1).split(";")
    assert tuple(values) == SHIPPED_LANGUAGE_ORDER, \
        f"chooser values {values} must be {list(SHIPPED_LANGUAGE_ORDER)}; append, never reorder"
    for language, endonym in FULL_TABLE_LANGUAGES.items():
        shown = tables[language]["#str_229908"].split(";")[values.index(language)]
        assert shown == endonym, f"{language}: the chooser names its own language {shown!r}, expected {endonym!r}"
    for path in ("guis/mainmenu.gui", "guis/menu/settings/game.gui"):
        commands = re.findall(r'CVarStrcmp sys_lang curr_lang ([A-Za-z ]+)"', read("content/baseoq4/pak0/" + path))
        assert commands, f"{path}: missing language-index command"
        for command in commands:
            assert command.lower().split() == values, f"{path}: language indices differ from chooser"

    # Each language's own letters, in its text and in every face that draws it.
    # All of them are Latin-1, Latin Extended-A or Cyrillic, so they come from
    # the base slots or pages the rasteriser always builds.
    for language, letters in SIGNATURE_LETTERS.items():
        missing = sorted(set(letters) - set("".join(tables[language].values())))
        assert not missing, f"{language}: tables never use {''.join(missing)}; wrong language or lost encoding?"
        for font in FONTS_DIR.glob("*.ttf"):
            if font.stem not in LATIN_ONLY_FACES:
                assert set(map(ord, letters)) <= font_code_points(font), f"{font.name}: missing {language} glyphs"


def validate_openq4_placeholders() -> None:
    """Every language's openQ4 table keeps the English printf placeholders in
    order, so a format the engine fills (the SYSTEM display lists) never reads
    the wrong argument. Partly translated tables are checked for their keys."""
    entry = re.compile(r'^\s*"(#str_\w+)"\s+"((?:\\.|[^"\\])*)"', re.MULTILINE)
    formats = re.compile(r'%(?:[-+0#]*\d*(?:\.\d+)?(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcsp]|%)')
    english = dict(entry.findall((STRINGS_DIR / "english_openq4.lang").read_text(encoding="utf-8")))
    for path in sorted(STRINGS_DIR.glob("*_openq4.lang")):
        for key, translated in entry.findall(path.read_text(encoding="utf-8")):
            original = english.get(key)
            if original is not None:
                assert formats.findall(original) == formats.findall(translated),                     f"{path.name}: {key}: printf arguments changed"


def validate_ci_smoke() -> None:
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")
    runner = read("tools/validation/openq4_validate.py")

    for source, context in (
        (push, "push verification workflow"),
        (commit, "commit validation workflow"),
        (runner, "validation runner"),
    ):
        require(source, "lang_table_encoding.py", context)


def main() -> None:
    validate_lang_files()
    validate_font_coverage()
    validate_transcode_contract()
    validate_code_point_font_path()
    validate_bitmap_font_policy()
    validate_codepage_selection()
    validate_language_menu()
    validate_translated_tables()
    validate_openq4_placeholders()
    validate_ci_smoke()
    print("lang_table_encoding: ok")


if __name__ == "__main__":
    main()
