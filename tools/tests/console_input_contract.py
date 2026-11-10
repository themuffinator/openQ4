#!/usr/bin/env python3
"""Pin the console's scrolling, caret, history and key routing.

tools/tests/native/ConsoleInputTest.cpp runs the rules in
src/framework/ConsoleInput.h. This contract pins that idConsoleLocal is wired to
them, and that the key routing GitHub issue #214 and the audit behind it fixed
stays as fixed: no engine-side key repeat, Escape handled by the open console,
Home and End left to the edit line, the wheel routed by the pointer.
"""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def function_body(text: str, head: str) -> str:
    """Return the definition whose signature starts with head, skipping declarations."""
    match = re.search(re.escape(head) + r"[^;{}]*\{", text)
    if match is None:
        raise AssertionError(f"Missing definition of {head!r}")
    depth = 0
    for index in range(match.end() - 1, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[match.start() : index + 1]
    raise AssertionError(f"Unbalanced body for {head!r}")


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, needle: str, context: str) -> None:
    if needle not in text:
        raise AssertionError(f"{context}: missing {needle!r}")


def forbid(text: str, needle: str, context: str) -> None:
    if needle in text:
        raise AssertionError(f"{context}: still contains {needle!r}")


def require_before(text: str, first: str, second: str, context: str) -> None:
    a = text.find(first)
    b = text.find(second)
    if a < 0 or b < 0 or a > b:
        raise AssertionError(f"{context}: {first!r} must come before {second!r}")


def validate_header() -> None:
    header = read("src/framework/ConsoleInput.h")
    for name in (
        "inline float SmoothScrollAdvance(",
        "inline float HoverAdvance(",
        "inline bool CaretVisible(",
        "inline bool ShouldRecordHistoryLine(",
        "inline int HistoryOlder(",
        "inline int HistoryNewer(",
    ):
        require(header, name, "ConsoleInput.h")
    # The rules stay free of engine types so the native test can include them.
    for include in re.findall(r'#include\s+[<"]([^>"]+)[>"]', header):
        if include != "cmath":
            raise AssertionError(f"ConsoleInput.h: unexpected include {include!r}")


def validate_scrolling(source: str) -> None:
    # Issue #214. The engine repeated Page Up / Page Down itself, once per
    # drawn frame, on top of the system's key repeat.
    for needle in ("lastKeyEvent", "nextKeyEvent", "CONSOLE_FIRSTREPEAT", "CONSOLE_REPEAT", "idConsoleLocal::Scroll("):
        forbid(source, needle, "Console.cpp")
    if re.search(r"idKeyInput::IsDown\(\s*K_PG(UP|DN)\s*\)", source):
        raise AssertionError("Console.cpp: Page Up / Page Down must not be polled; a held key repeats through key events")

    draw = function_body(source, "void	idConsoleLocal::Draw( bool forceFullScreen )")
    require_before(draw, "UpdateFrameTime();", "UpdateDisplayLine();", "Draw")
    require(draw, "UpdateCaretBlink();", "Draw")
    forbid(draw, "Scroll();", "Draw")

    frame = function_body(source, "void idConsoleLocal::UpdateFrameTime( void )")
    require(frame, "Sys_GetClockTicks()", "UpdateFrameTime")
    forbid(frame, "Milliseconds()", "UpdateFrameTime")

    smooth = function_body(source, "void idConsoleLocal::UpdateDisplayLine( void )")
    require(
        smooth,
        "oq4console::SmoothScrollAdvance( displayLine, target, frameSeconds, con_scrollSmoothSpeed.GetFloat() )",
        "UpdateDisplayLine",
    )
    forbid(smooth, "Milliseconds()", "UpdateDisplayLine")

    hover = function_body(source, "void idConsoleLocal::UpdateScrollbarHoverValue(")
    require(hover, "oq4console::HoverAdvance(", "UpdateScrollbarHoverValue")
    require(hover, "CON_SCROLLBAR_LERP_SPEED", "UpdateScrollbarHoverValue")

    key_down = function_body(source, "void idConsoleLocal::KeyDownEvent( int key )")
    require(key_down, "if ( key == K_PGUP || key == K_MWHEELUP ) {", "KeyDownEvent")
    require(key_down, "if ( key == K_PGDN || key == K_MWHEELDOWN ) {", "KeyDownEvent")
    # Ctrl+Home / Ctrl+End must be tested before the edit line takes Home / End.
    require_before(key_down, "Top();", "if ( InputKeyDownEvent( key ) )", "KeyDownEvent")
    require_before(key_down, "Bottom();", "if ( InputKeyDownEvent( key ) )", "KeyDownEvent")
    # A command runs from the end of the log.
    enter = key_down[key_down.find("if ( key == K_ENTER || key == K_KP_ENTER )"):]
    require_before(enter, "Bottom();", "cmdSystem->BufferCommandText( CMD_EXEC_APPEND, consoleField.GetBuffer() );", "KeyDownEvent Enter")


def validate_escape(source: str) -> None:
    key_down = function_body(source, "void idConsoleLocal::KeyDownEvent( int key )")
    escape = key_down[key_down.find("if ( key == K_ESCAPE ) {"):]
    if not escape.startswith("if ( key == K_ESCAPE ) {"):
        raise AssertionError("KeyDownEvent: missing the Escape branch")
    escape = escape[: escape.find("return;")]
    require_before(escape, "DismissCompletionPopup();", "CloseFromInput();", "KeyDownEvent Escape")

    process = function_body(source, "bool	idConsoleLocal::ProcessEvent( const sysEvent_t *event, bool forceAccept )")
    require(process, "if ( forceAccept && event->evValue == K_ESCAPE ) {", "ProcessEvent")

    session = read("src/framework/Session.cpp")
    process_event = function_body(session, "bool idSessionLocal::ProcessEvent( const sysEvent_t *event )")
    require(process_event, "( event->evValue == K_ESCAPE && !console->Active() )", "idSessionLocal::ProcessEvent")


def validate_popup_keys(source: str) -> None:
    body = function_body(source, "bool idConsoleLocal::InputKeyDownEvent( int key )")
    popup = body[body.find("if ( HasActiveCompletionPopup() ) {"):]
    popup = popup[: popup.find("if ( idKeyInput::IsDown( K_CTRL ) ) {")]
    for needle in ("case K_HOME:", "case K_END:"):
        forbid(popup, needle, "InputKeyDownEvent popup keys")
    wheel = popup[popup.find("case K_MWHEELUP:"):]
    if not wheel.startswith("case K_MWHEELUP:"):
        raise AssertionError("InputKeyDownEvent: missing the popup wheel case")
    require_before(wheel, "if ( !IsPointInCompletionPopup( mouseX, mouseY ) ) {", "completionSelection =", "InputKeyDownEvent popup wheel")


def validate_history(source: str) -> None:
    key_down = function_body(source, "void idConsoleLocal::KeyDownEvent( int key )")
    require(key_down, "oq4console::ShouldRecordHistoryLine( consoleField.GetBuffer()", "KeyDownEvent")
    require(key_down, "RecallHistoryLine( oq4console::HistoryOlder( historyLine, nextHistoryLine, COMMAND_HISTORY ) );", "KeyDownEvent")
    require(key_down, "RecallHistoryLine( oq4console::HistoryNewer( historyLine, nextHistoryLine ) );", "KeyDownEvent")
    forbid(key_down, "consoleField = historyEditLines[", "KeyDownEvent")

    recall = function_body(source, "void idConsoleLocal::RecallHistoryLine( int line )")
    require_before(recall, "historyDraft = consoleField;", "historyLine = line;", "RecallHistoryLine")
    require(recall, "consoleField = historyDraft;", "RecallHistoryLine")

    load = function_body(source, "void idConsoleLocal::LoadCommandHistory( void )")
    # A purged private command has to be noticed before a repeat or blank line
    # could skip it, or the history file would never be rewritten without it.
    require_before(load, "CommandContainsPrivateCVar( line.c_str() )", "oq4console::ShouldRecordHistoryLine( line.c_str()", "LoadCommandHistory")

    open_for_input = function_body(source, "void idConsoleLocal::OpenForInput( bool reduced )")
    require(open_for_input, "historyLine = nextHistoryLine;", "OpenForInput")


def validate_editing(source: str) -> None:
    draw_input = function_body(source, "void idConsoleLocal::DrawInputText( float x, float y, bool showCursor ) const")
    require(draw_input, "oq4console::CaretVisible( caretClock )", "DrawInputText")
    forbid(draw_input, "com_ticNumber", "DrawInputText")

    paste = function_body(source, "void idConsoleLocal::PasteClipboardToInput( void )")
    require(paste, "InsertInputTextAt( text, consoleField.GetCursor() );", "PasteClipboardToInput")
    require_before(paste, "memset( text, 0, strlen( text ) );", "Mem_Free( text );", "PasteClipboardToInput")

    insert = function_body(source, "void idConsoleLocal::InsertInputTextAt( const char *text, int cursor )")
    require_before(insert, "completionRebuildHold++;", "InsertInputChar( ch );", "InsertInputTextAt")
    require_before(insert, "completionRebuildHold--;", "RebuildCompletionState();", "InsertInputTextAt")

    rebuild = function_body(source, "void idConsoleLocal::RebuildCompletionState( void )")
    require_before(rebuild, "if ( completionRebuildHold > 0 ) {", "RefreshCompletionState();", "RebuildCompletionState")


def validate_registration() -> None:
    meson = read("meson.build")
    native_block = meson[meson.find("if get_option('build_native_tests')"):]
    native_block = native_block[: native_block.find("\nendif")]
    require(native_block, "'tools/tests/native/ConsoleInputTest.cpp'", "meson.build native tests")
    require(native_block, "'openq4-console-input'", "meson.build native tests")

    validator = read("tools/validation/openq4_validate.py")
    require(validator, '"console_input_contract.py"', "openq4_validate.py")


def main() -> None:
    source = read("src/framework/Console.cpp")
    require(source, '#include "ConsoleInput.h"', "Console.cpp")
    validate_header()
    validate_scrolling(source)
    validate_escape(source)
    validate_popup_keys(source)
    validate_history(source)
    validate_editing(source)
    validate_registration()


if __name__ == "__main__":
    main()
