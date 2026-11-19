# Text encoding and localization

openQ4 text is UTF-8 end to end. A string table is authored in it, a retail
8-bit table is normalised to it as it loads, and the GUI draws by Unicode code
point. What a language can say is therefore limited by what the fonts have art
for, not by what one 8-bit codepage can express.

This replaced the opposite arrangement, which is worth stating plainly because
it is the reason several things here look inverted from the retail engine.
Quake 4 draws text from 256 byte-indexed glyphs, so a byte in a string table and
a glyph in an atlas had to agree on which character they meant. The engine used
to enforce that by transcoding UTF-8 tables *down* to an 8-bit codepage at load
time. That worked, and it is how Polish shipped, but it capped every language at
whatever one codepage could hold — and no codepage covers Cyrillic *and* has
retail atlas art, so Russian was simply unreachable.

## The pipeline

```
 .lang file ──► idLangDict::Load ──► UTF-8 string ──► idDeviceContext::DrawText
   (UTF-8 or                          (always)          │
    8-bit retail)                                       ├─ decode one code point
                                                        └─ R_GlyphForCodePoint
                                                             ├─ U+0000-U+00FF → base slot
                                                             └─ above        → extended page
```

### Loading

`LangDict_ConvertCodePageToUtf8` in `src/idlib/LangDict.cpp` decides what a file
is by asking whether the whole buffer is well-formed UTF-8. That is a reliable
test rather than a heuristic: a retail 8-bit table is never valid UTF-8 end to
end, because a byte in the 0xC0-0xFF range is read as a lead byte and the ASCII
letter after it fails the continuation-byte test. A pure-ASCII table answers yes
and passes through untouched.

Which codepage a legacy table is decoded as follows `sys_lang`, and it has to:
the byte values overlap completely. 0xB9 is superscript one in Windows-1252,
a-ogonek in Windows-1250 and a soft hyphen in Windows-1251. There is nothing in
the bytes to tell them apart, which is why `LangDict_SetActiveCodePage` must run
before the first `languageDict.Load()` — one dictionary cannot hold two
codepages.

### Drawing

`R_GlyphForCodePoint` in `src/renderer/RenderSystem.h` is the single place that
knows how a `fontInfo_t` is indexed, because there are two answers:

- **`GLYPH_INDEX_UNICODE`** — the scalable path. The 256 base slots are Latin-1,
  so slot 0xE9 is U+00E9 whatever the language, and everything above U+00FF
  lives in a sparse page directory hung off the font.
- **`GLYPH_INDEX_CODEPAGE`** — the retail `.fontdat` atlases. 256 slots of art
  for one 8-bit codepage, so a code point is mapped back onto a byte and
  anything that codepage cannot express has no glyph at all.

The function returns NULL rather than a blank when there is no art, which the
text layer has to handle rather than paper over: an empty glyph has a zero
advance, so the character would silently vanish instead of showing as
unavailable. `openQ4_NextTextGlyph` substitutes — the ASCII fold for punctuation
the retail atlases never carried, a question mark otherwise.

Every pass over text — drawing, measuring, wrapping — steps through
`openQ4_NextTextGlyph`, so they cannot disagree about where one character ends
and the next begins. A measure pass and a draw pass that split a two-byte
character differently would put the ink, the wrap point and the edit cursor in
three places.

Byte offsets stay byte offsets throughout. The cursor positions and limits the
GUI passes into `DrawText` are indices into the string it handed over;
reinterpreting them as character counts would move every caller's cursor.

Not everything drawn came from a string table — a cvar value, a server name, a
console line can hold anything — so `LangDict_NextCodePoint` never fails and
always advances. A byte that cannot start a well-formed sequence is read as one
byte through the **active codepage**, not as Latin-1: a buffer that is not valid
UTF-8 is by definition legacy 8-bit text, and the codepage is the session's best
statement about which one. That is what turns a stray Windows-1252 smart quote
into U+201C, which every face has art for, rather than U+0093, which is a
control code no face covers.

### Extended glyph pages

Code points above U+00FF live in 256-entry pages (`fontGlyphPage_t`), indexed by
`code point >> 8`, stopping at the Basic Multilingual Plane. Each page carries a
`covered[]` mask, because a page is built for a whole Unicode block but a face
rarely covers all of it — without the mask a hole is indistinguishable from a
real glyph that happens to be zero width.

Pages are packed into their own atlas, separate from the base one, so a page
glyph and a base glyph in the same string come from two different images. That
is why `R_GlyphForCodePoint` returns a material alongside the glyph.

`R_TTFCollectExtendedCodePoints` decides what to build, from three sources:

1. `Q4_TTF_UNIVERSAL_RANGES` — Latin Extended-A/B, Spacing Modifier Letters,
   General Punctuation, Currency and Letterlike Symbols. **The Latin blocks are
   not optional.** Windows-1252 reaches outside Latin-1 in its 0x80-0x9F band —
   o-e ligature, s-caron, florin, y-diaeresis — and the shipped French, Spanish
   and Italian tables use those characters. As single bytes they landed in the
   base 256 slots; as UTF-8 they are code points above U+00FF and need real
   pages. Latin Extended-A also covers the Central European alphabets, so Polish
   and Czech need nothing of their own.
2. `LangDict_ExtendedRangesForLanguage` — non-Latin scripts the active language
   declares, currently Cyrillic for Russian. Whole blocks rather than the exact
   set a table happens to use, so a player name or a console line typed at
   runtime draws in the same alphabet as the menus around it.
3. A sweep of the loaded string tables, which catches whatever a language needs
   that nobody declared. This is what makes adding a language a matter of
   authoring its tables rather than of editing the renderer.

The face is consulted last: only code points it actually has art for are
rasterised, so asking for a whole block costs nothing on a face that carries
none of it, and `strogg` — Latin-only by design — ends up with no extended pages
at all rather than a page full of blanks.

Page sets are allocated once per (face, point size) and refilled in place on
re-registration, never freed before renderer shutdown. A `fontInfo_t` is copied
by value all over the GUI, and freeing a set would strand every copy still
pointing at it with no way to tell which those are.

### The console

The console and loading screen slice characters out of a 16x16 grid of cells
indexed by byte, and that stayed byte-indexed — reworking those draw paths buys
nothing. Instead its 256 cells are a **codepage** rather than Latin-1, and
`Con_ConsoleCellForCodePoint` maps a code point onto one. The sheet is rebuilt
whenever the active codepage changes, so a Russian session gets a Cyrillic
console and a Polish one gets a Polish console.

`idConsoleLocal::PrintToBuffer` does that mapping on the way *in*, not at draw
time, because a console buffer cell has eight bits for the character and eight
for the colour — no room for a code point. Mapping early is also what keeps the
column arithmetic honest: a two-byte Cyrillic letter occupies one column, and
word wrap counts columns.

The cost is that the console shows one script at a time. The GUI, which is where
localized text actually lives, has no such limit.

## The legacy bitmap font

`r_useTrueTypeFonts 0` still returns to the retail atlases, and that is worth
keeping: it is the fallback when a mod ships its own `.fontdat`, and
`uiFontParitySelfTest` pins it to assert retail parity.

But it is **ignored for languages the atlases cannot draw**. A retail atlas has
256 slots of Latin art; Windows-1251 exists, but no shipped `.fontdat` has
Cyrillic glyphs, so every byte would land on a blank slot and Russian or
Ukrainian would render as a menu of question marks with nothing to explain it.
Czech, Hungarian and Turkish are subtler and worse: they have no retail release,
so the only atlases they find are the English ones, which are Windows-1252 art.
Their own codepage bytes then index the wrong letters - c-caron (0xE8 in
Windows-1250) draws as e-grave, and s-cedilla (0xFE in Windows-1254) as thorn.
Polish keeps the bitmap path, because the retail Polish release shipped its own
Central European atlases. `R_UseScalableFonts` therefore lets the language win,
and says so once on the console. The cvar is deliberately not written back: it
is archived, and quietly rewriting a user's setting because they tried a
language would leave the bitmap path off after they switched away again.

`LangDict_LanguageNeedsScalableFonts` is the predicate. Add to it when adding a
language whose letters the retail atlases do not carry.

## Adding a language

1. Author `strings/<language>_{code,guis,maps,mappack,openq4}.lang` in UTF-8, no
   BOM, with the **same keys in the same order** as the English files.
   `choiceDef` and other choice lists are positional, so every value keeps the
   English number of `;` - a dropped one silently mismatches every entry after
   it. The name before the first `_` is the `sys_lang` value, so it cannot
   contain an underscore.
2. Add the name to `sysLanguageNames` (`src/sys/sys_local.cpp`) and
   `fsLanguagePackOrder` (`src/framework/FileSystem.cpp`) if it is not already
   there, and map its OS locale in `Common_MapLocaleLanguageCode` and
   `Common_MapWindowsPrimaryLanguage`. String tables for a name missing from
   `sysLanguageNames` are ignored.
3. **Append** it to the chooser's `values` list in
   `guis/menu/settings/game.gui` and to both `CVarStrcmp sys_lang curr_lang`
   commands (`game.gui`, `mainmenu.gui`), and append its name to `#str_229908`
   in **every** language's `guis` and `openq4` tables. The lists are positional,
   and appending keeps every shipped index - and every archived `sys_lang` -
   where it was.
4. Choose its legacy codepage in `LangDict_CodePageForLanguage`; that also picks
   the console's 256 cells. If the script is not Latin, add its Unicode block to
   `LangDict_ExtendedRangesForLanguage`. If the retail atlases cannot draw it,
   add it to `LangDict_LanguageNeedsScalableFonts`. If its keyboard layouts put
   characters on AltGr, add it to the right-Alt lists in `Sys_InitScanTable`
   (`src/sys/sdl3/sdl3_backend.cpp` and `src/sys/win32/win_input.cpp`, which is
   an 8-bit file: edit it byte-wise).
5. Add it to `LANGUAGES` in `tools/ui/build_retained_screens.py` and rerun the
   generator, so the retained screens fit its text; the retained gate then
   checks every loading tip fits two lines in it.
6. Add it to `FULL_TABLE_LANGUAGES`, `SIGNATURE_LETTERS` and
   `SHIPPED_LANGUAGE_ORDER` in `tools/tests/lang_table_encoding.py`, and to the
   language lists of the contract tests that check per-language keys
   (competitive and Match Control localization, server browser, difficulty
   restart, SYSTEM presets). Then run them.
7. Run the SYSTEM page's native fit tests (`tools/tests/native/UiSystem*Test.cpp`
   and `UiPopupPlacementRuntimeTest.cpp`) for it. They lay the retained page out
   with its real tables at every UI scale and fail on any label that does not
   fit. CI runs a seven-locale sample (English, Spanish, Polish, Russian,
   French, Italian and German, which has the longest labels) to keep its time
   down, so add the language to their lists locally, rebuild, and pass it as
   the last argument.

`tools/tests/lang_table_encoding.py` checks the tables against English (keys,
order, printf arguments, `^` escapes, layout escapes, semicolons), the chooser
lists and indices, the engine language lists and OS-locale maps, and that every
code point used is one the fonts actually have art for.
`tools/tests/localization_smoke.py --language <name>` then qualifies the staged
runtime in game.

`ListAvailableLanguagePacks` only sees retail `zpak_<language>` media archives,
which is why a repo-authored language would otherwise be rejected before its
tables were consulted. `Common_AppendLanguagesWithStringTables` unions in
languages that have `strings/*.lang`, so a text-only language is selectable
(English audio, translated text).

## German localization

Select **Deutsch** under Settings > Game Options > Language, or launch with
`+set sys_lang german`. First-run OS language detection already maps German
locales to `german`. Text-only installations are selectable through the string
table discovery path, without requiring a German voice archive.

The five `german_{code,guis,maps,mappack,openq4}.lang` tables cover all 3,196
entries in their English counterparts: menus, settings, HUD and multiplayer
messages, objectives, in-world terminals, Arena Campaign and demo playback.
Multiplayer map names, character names, command tokens and technical identifiers
retain their original spelling. German uses the existing Windows-1252 legacy
decoder and Latin-1 font glyphs, including umlauts and sharp s.

Campaign dialogue is supplied by the player's retail installation. There is no
bundled `german_lips.lang` override: installed German subtitles and dialogue
timing remain available, and German voice samples take priority when present.
Without German dialogue assets, the existing English subtitle/voice fallback
applies. Install the complete retail `q4base/zpak_german.pk4` and its applicable
patch archives for German dialogue; numbered patch archives alone are not a
complete voice pack. Restart after installing dialogue assets or changing
language to refresh already cached voices.

The language chooser appends German to preserve existing language indices.
Its GUI reload forces a reparse after command dispatch: translating labels
does not change the GUI file timestamps, and a timestamp-only reload would
leave the previous language visible. Active GUIs rerun their activation scripts
after reparsing so the menu fade-in and visibility are restored.
`tools/tests/lang_table_encoding.py` checks German key coverage and order,
printf arguments, GUI icon/color escapes, line breaks, choice counts, chooser
indices and font coverage. The competitive localization, Match Control,
server-browser and difficulty-restart contracts also include German.

`tools/tests/german_localization_smoke.py` checks the staged runtime with retail
assets and Pillow. It follows the mode-specific launch configuration, uses a
hidden window with mouse/controller input disabled, and captures only through
the engine's `screenshot` command. The SP case enters `game/airdefense1`, runs
the real language-chooser callback from a temporary GUI timeline, and verifies
German-to-English-to-German labels plus nonblank menu captures. Its fixture uses
independent copies of staged files. The MP case enters `mp/q4dm1` and verifies
the translated Match Control view. For example, from the repository root:

```powershell
python tools/tests/german_localization_smoke.py --mode SP --renderer gl --basepath "E:\SteamLibrary\steamapps\common\Quake 4"
python tools/tests/german_localization_smoke.py --mode MP --renderer vulkan --basepath "E:\SteamLibrary\steamapps\common\Quake 4"
```

Logs, scripts and engine captures go under `.tmp/german-localization-smoke/` by
default. Windows x64 SP/OpenGL and MP/Vulkan have been validated with English
retail dialogue fallback. A complete German retail voice pack was unavailable
on the validation machine, so German voice playback itself was not qualified.

## Brazilian Portuguese, Czech, Hungarian, Turkish and Ukrainian

These five were chosen as the most-used Steam languages Quake 4 never shipped
whose scripts the existing faces already draw (Steam survey, September 2026:
Brazilian Portuguese 3.7%, Turkish 1.1%, Ukrainian 0.66%, Czech 0.50%,
Hungarian 0.32%). The larger missing languages - Simplified and Traditional
Chinese, Japanese, Korean, Thai - need new font art and line breaking without
spaces, which is separate work.

| `sys_lang` | Chooser | Codepage (console, legacy tables) | Bitmap atlases | OS locale |
|---|---|---|---|---|
| `brazilian` | Português (Brasil) | Windows-1252 | usable | `pt`, `LANG_PORTUGUESE` |
| `czech` | Čeština | Windows-1250 | never | `cs`, `LANG_CZECH` |
| `hungarian` | Magyar | Windows-1250 | never | `hu`, `LANG_HUNGARIAN` |
| `turkish` | Türkçe | Windows-1254 | never | `tr`, `LANG_TURKISH` |
| `ukrainian` | Українська | Windows-1251 | never | `uk`, `LANG_UKRAINIAN` |

Each ships `<language>_{code,guis,maps,mappack,openq4}.lang`, translating every
entry of the English tables, as German does. Like German, campaign dialogue
is not translated: these languages never had a retail voice pack, so
`SoundSample_AppendLocalizedVOVariants` falls back to the English voices.
Character names, multiplayer map names, key names, command tokens and
technology names keep their original spelling. Every letter is Latin-1, Latin
Extended-A or Cyrillic, so the universal and Cyrillic pages draw them all.
The faces do lack a few Latin-1 symbols (`º`, `ª`, superscript digits,
fractions); the tables avoid them, using `°` for Portuguese ordinals.

Turkish has its own codepage, Windows-1254. It differs from Windows-1252 in
six letters (G-breve, dotless i, capital dotted I and S-cedilla in both cases)
and leaves 0x8E/0x9E unassigned. Without it the console's 256 cells would hold
Icelandic letters where Turkish needs its own. All non-Western codepages now
go through one lookup, `LangDict_HighTableForCodePage`.

Czech, Hungarian and Turkish join the Cyrillic languages in overriding
`r_useTrueTypeFonts 0` (see [The legacy bitmap font](#the-legacy-bitmap-font)).
Their alphabets fit an 8-bit codepage, but the only atlases they can find are
the English Windows-1252 ones, which would draw wrong letters rather than
missing ones. Brazilian Portuguese keeps the bitmap path, since every letter it
uses is in Windows-1252 art.
`openQ4_NormalizeFontLanguage` also folds `brazilian` onto the English
artwork, as it does the Western retail languages.

Their keyboards put characters on AltGr: `@`, `/` and `?` on ABNT2, and
the Ukrainian ghe with upturn. Right Alt therefore binds as `K_RIGHT_ALT` for all
five, as it does for Polish and the Western retail languages. Typing is still
limited to code points up to U+00FF (see below). Brazilian Portuguese is fully
typeable. In the other four, letters outside Latin-1 cannot be entered in chat,
the console or name fields yet.

The tables were translated from English with a per-language glossary of
terms, form of address and capitalisation rules. Those rules include
Turkish's dotted capital İ, Czech and Hungarian sentence case, and Ukrainian
`’` apostrophes and `«»` quotes. The Portuguese locale `pt` maps to
`brazilian`; a European Portuguese system gets the closest available
table rather than English.

`tools/tests/localization_smoke.py` generalises the German smoke. It reads
the expected labels from the staged tables, so any complete language runs:

```powershell
python tools/tests/localization_smoke.py --language turkish --mode SP --renderer gl --basepath "E:\SteamLibrary\steamapps\common\Quake 4"
python tools/tests/localization_smoke.py --language ukrainian --mode MP --renderer vulkan --basepath "E:\SteamLibrary\steamapps\common\Quake 4"
```

`german_localization_smoke.py` remains as the German case with its original
evidence folder.

## Russian gameplay text

Russian started with only the `guis` and `openq4` tables. Its `code` and
`maps` tables are now translated from the English tables, the same way as the
five languages above, so Russian is in `FULL_TABLE_LANGUAGES`.
`russian_mappack.lang` keeps the map names, as the other languages' do.

- **Source.** The translation was made from English only, using the Russian
  `guis` and `openq4` tables as its glossary. Pull request #243 offered tables
  based on a commercial retail Russian localisation; they were not used, so
  that no third-party translation is redistributed.
- **Shape.** Strings the game formats keep the English shape. The tournament
  placings keep their `^0` colour resets, the fourth-and-later placing starts
  with its ordinal ending (`-е^0 место`) because the game prints the rank
  number straight before it, and the ready-up prompts keep their `\n` and
  semicolons.
- **Names.** Multiplayer map names, joystick and AUX key names, the `DM` and
  `CTF` abbreviations and the product name `openQ4` keep their original
  spelling. Character names are written in Cyrillic, as the existing Russian
  tables already did (Восс, Следж, Штраусс), and so are ranks.
- **One table per key.** `russian_openq4.lang` had carried 56 `code` keys while
  Russian had no `code` table. They now live in `russian_code.lang` with the
  same text. The openQ4 table loads last, so its text was the one in effect,
  and what players see for those keys does not change.

Campaign subtitles (`russian_lips.lang`) are not bundled. As with German, an
installation's own Russian dialogue table stays in effect, and otherwise the
English subtitles apply.

## What is not covered

- **Text input above Latin-1.** `idEditField` and the win32/SDL scan tables are
  byte-oriented, and the SDL text path drops committed code points above U+00FF.
  Typing Cyrillic, or the Czech, Hungarian and Turkish letters outside Latin-1,
  into the console or a chat box is not wired up. Display is complete; entry is
  not.
- **Shaping and bidi.** The faces carry Arabic Presentation Forms-B ready for a
  shaper, but there is none, and no bidirectional layout.
- **Non-BMP code points.** The page directory stops at U+FFFF.
- **Case folding and collation.** `idStr::ToUpper`/`ToLower` work on 8-bit
  codepage bytes and would mangle a multi-byte sequence. Nothing in the display
  path applies them to user-visible text — the one call in `idDeviceContext` is
  guarded on a single-byte key name, and the ones in `idWindow` operate on GUI
  variable names — but a new caller must not assume otherwise.
- **`strogg.ttf`** covers only Latin runes. It draws hardcoded credits text and
  is never asked for a string table, so this does not affect any language.
