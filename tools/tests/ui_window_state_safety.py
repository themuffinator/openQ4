#!/usr/bin/env python3
"""Guard GUI timeline parsing and transactional window save-state handling."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INT_MIN = -(1 << 31)
INT_MAX = (1 << 31) - 1


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def between(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    finish = source.index(end, begin + len(start))
    return source[begin:finish]


def require(source: str, token: str, label: str) -> None:
    if token not in source:
        raise AssertionError(f"{label} is missing {token!r}")


def parse_timeline_milliseconds(text: str) -> int | None:
    if not text:
        return None
    negative = text[0] == "-"
    if text[0] in "+-":
        text = text[1:]
    if not text:
        return None
    limit = 1 << 31 if negative else INT_MAX
    magnitude = 0
    for character in text:
        if character < "0" or character > "9":
            return None
        digit = ord(character) - ord("0")
        if magnitude > (limit - digit) // 10:
            return None
        magnitude = magnitude * 10 + digit
    return -magnitude if negative else magnitude


def add_relative(previous: int, value: int) -> int | None:
    combined = previous + value
    return combined if INT_MIN <= combined <= INT_MAX else None


def validate_timeline_model() -> None:
    valid = {
        "0": 0,
        "+0": 0,
        "-0": 0,
        "1": 1,
        "+17": 17,
        "-17": -17,
        "00042": 42,
        "2147483647": INT_MAX,
        "-2147483648": INT_MIN,
    }
    for text, expected in valid.items():
        actual = parse_timeline_milliseconds(text)
        if actual != expected:
            raise AssertionError(f"timeline parser rejected {text!r}: expected {expected}, got {actual}")
    for text in ("", "+", "-", " 1", "1 ", "1.0", "0x10", "++1", "--1", "2147483648", "-2147483649", "１"):
        if parse_timeline_milliseconds(text) is not None:
            raise AssertionError(f"timeline parser accepted malformed or overflowing value {text!r}")
    if add_relative(INT_MAX - 7, 7) != INT_MAX or add_relative(INT_MIN + 7, -7) != INT_MIN:
        raise AssertionError("timeline relative parser rejected a signed boundary")
    if add_relative(INT_MAX, 1) is not None or add_relative(INT_MIN, -1) is not None:
        raise AssertionError("timeline relative parser accepted signed overflow")


def validate_parser_contract(window: str) -> None:
    helper = between(window, "static bool OpenQ4_ParseTimelineMilliseconds", "bool idWindow::Parse(")
    for token in (
        "uint64 magnitude = 0;",
        "negative ? 2147483648ULL : 2147483647ULL",
        "magnitude > ( magnitudeLimit - digit ) / 10ULL",
        "const int64 signedValue",
    ):
        require(helper, token, "GUI timeline integer parser")

    on_time = between(window, 'else if ( token == "onTime" )', 'else if ( token == "definefloat" )')
    for token in (
        "OpenQ4_ParseTimelineMilliseconds( timeToken, eventTime )",
        "const int64 combinedTime",
        "combinedTime < idMath::INT_MIN || combinedTime > idMath::INT_MAX",
        "idTimeLineEvent *ev = new idTimeLineEvent;",
        "delete ev;",
    ):
        require(on_time, token, "GUI timeline event parser")
    if on_time.index("idTimeLineEvent *ev = new idTimeLineEvent;") < on_time.index(
        "OpenQ4_ParseTimelineMilliseconds( timeToken, eventTime )"
    ):
        raise AssertionError("GUI timeline event is allocated before its timestamp is validated")

    named = between(window, 'else if ( token == "onNamedEvent" )', 'else if ( token == "onTime" )')
    require(named, "delete ev;", "GUI named-event parse failure cleanup")
    require(window, "idStr::Copynz( p, token.c_str(), token.Length() + 1 );", "GUI deferred-variable copy")
    if "strcpy( p, token.c_str() )" in window:
        raise AssertionError("GUI deferred-variable parsing retains an unbounded copy")


def validate_save_contract(window: str, header: str, winvar: str, simple: str) -> None:
    for token in (
        "static ID_INLINE bool OpenQ4_WriteSaveGameBytes",
        "static ID_INLINE bool OpenQ4_ReadSaveGameBytes",
        "static ID_INLINE bool OpenQ4_WriteSaveGameField",
        "static ID_INLINE bool OpenQ4_ReadSaveGameField",
        "memset( buffer, 0, len );",
        "value = false;",
        "value = 0;",
        "value = 0.0f;",
        "if ( !std::isfinite( value ) )",
    ):
        require(winvar, token, "GUI save primitive")
    for token in (
        "bool\t\t\tWriteSaveGameString",
        "bool\t\t\tWriteSaveGameTransition",
        "bool\t\t\tWriteSaveGameChildReference",
        "bool\t\t\tReadSaveGameString",
        "bool\t\t\tReadSaveGameTransition",
        "bool\t\t\tFixupTransitions",
        "bool BuildSaveGameChildOrder",
        "bool *readSucceeded = NULL",
    ):
        require(header, token, "GUI save API")

    writer = between(window, "void idWindow::WriteToSaveGame( idFile *savefile )", "bool idWindow::ReadSaveGameString")
    for token in (
        "savefile == NULL || gui == NULL",
        "refusing non-finite or out-of-range layout state",
        "OpenQ4_WriteSaveGameField",
        "WriteSaveGameChildReference",
        "incomplete timeline event",
        "incomplete named event",
        "invalid simple/full ownership",
    ):
        require(writer, token, "GUI window save writer")

    reader = between(window, "void idWindow::ReadFromSaveGame( idFile *savefile )", "int idWindow::NumTransitions()")
    for token in (
        "idStr savedCmd;",
        "idStr savedName;",
        "non-finite layout state",
        "saved child id",
        "saved structural flags",
        "savedName.Icmp( name ) != 0",
        "bool focusReadSucceeded = false;",
        "!focusReadSucceeded || !captureReadSucceeded || !overReadSucceeded",
        "invalid transition count",
        "ValidateRestoredTrackedWindowPointers",
        "!FixupTransitions( OpenQ4_SaveGameRestoreIsPortable( savefile ) )",
    ):
        require(reader, token, "GUI window save reader")
    if reader.index("cmd = savedCmd;") < reader.index("savedName.Icmp( name ) != 0"):
        raise AssertionError("GUI window restore commits header state before validating window identity")

    simple_writer = between(simple, "void idSimpleWindow::WriteToSaveGame", "void idSimpleWindow::ReadFromSaveGame")
    for token in (
        "savefile == NULL",
        "OpenQ4_IsFiniteSimpleWindowRectangle",
        "OpenQ4_WriteSaveGameField",
        "OpenQ4_WriteSaveGameInt",
        "OpenQ4_WriteSaveGameBytes",
        "64 * 1024",
    ):
        require(simple_writer, token, "simple GUI window save writer")
    simple_reader = simple[simple.index("void idSimpleWindow::ReadFromSaveGame") :]
    for token in (
        "idRectangle savedDrawRect;",
        "OpenQ4_ReadSaveGameField",
        "OpenQ4_IsFiniteSimpleWindowRectangle( savedDrawRect )",
        "OpenQ4_ReadSaveGameInt",
        "remainingBytes",
        "background = NULL;",
    ):
        require(simple_reader, token, "simple GUI window save reader")
    if simple_reader.index("drawRect = savedDrawRect;") < simple_reader.index(
        "OpenQ4_IsFiniteSimpleWindowRectangle( savedDrawRect )"
    ):
        raise AssertionError("simple GUI window restore commits layout before finite validation")


def main() -> None:
    window = read("src/ui/Window.cpp")
    validate_timeline_model()
    validate_parser_contract(window)
    validate_save_contract(window, read("src/ui/Window.h"), read("src/ui/Winvar.h"), read("src/ui/SimpleWindow.cpp"))
    require(read("tools/validation/openq4_validate.py"), "ui_window_state_safety.py", "GUI window validation wiring")
    print("GUI window state safety: ok")


if __name__ == "__main__":
    main()
