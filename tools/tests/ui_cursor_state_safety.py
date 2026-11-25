#!/usr/bin/env python3
"""Guard GUI cursor normalization and finite save/demo state restoration."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def between(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    finish = source.index(end, begin + len(start))
    return source[begin:finish]


def require(source: str, token: str, label: str) -> None:
    if token not in source:
        raise AssertionError(f"{label} is missing {token!r}")


def main() -> None:
    source = read("src/ui/UserInterface.cpp")
    header = read("src/ui/UserInterfaceLocal.h")
    winvar = read("src/ui/Winvar.h")

    require(header, "void\t\t\t\t\t\tClampCursor( void );", "GUI cursor helper declaration")
    set_cursor = between(source, "void idUserInterfaceLocal::SetCursor( float x, float y )", "idUserInterfaceLocal::ClampCursor")
    require(set_cursor, "ClampCursor();", "GUI absolute cursor route")

    handle_event = between(source, "const char *idUserInterfaceLocal::HandleEvent", "void idUserInterfaceLocal::HandleNamedEvent")
    require(handle_event, "SetCursor( cursorX + static_cast<float>( event->evValue )", "GUI relative cursor route")
    if "cursorX +=" in handle_event or "cursorY +=" in handle_event:
        raise AssertionError("GUI relative input bypasses the shared cursor normalization path")

    clamp_cursor = between(source, "void idUserInterfaceLocal::ClampCursor( void )", "bool idUserInterfaceLocal::GetMaxTextIndex")
    for token in (
        "!std::isfinite( cursorX )",
        "!std::isfinite( cursorY )",
        "GetCursorBounds( minX, minY, maxX, maxY );",
        "idMath::ClampFloat( minX, maxX, cursorX )",
        "idMath::ClampFloat( minY, maxY, cursorY )",
    ):
        require(clamp_cursor, token, "GUI cursor normalization")
    bounds = between(source, "void idUserInterfaceLocal::GetCursorBounds(", "void idUserInterfaceLocal::ClampCursor( void )")
    for token in (
        "std::isfinite( desktop->forceAspectWidth )",
        "std::isfinite( desktop->forceAspectHeight )",
        "std::isfinite( xExpand ) && xExpand >= 0.0f",
        "std::isfinite( yExpand ) && yExpand >= 0.0f",
    ):
        require(bounds, token, "GUI cursor bounds")
    # A menu's widescreen bounds must not depend on the aspect mode the shared
    # device context was last drawn with (PR #173).
    require(bounds, "dc.GetAspectCorrectedScreenExpansion( maxX, maxY, xExpand, yExpand )", "menu cursor widescreen bounds")
    if "dc.GetVirtualScreenExpansion(" in bounds:
        raise AssertionError("menu cursor bounds read the shared device context's last-drawn aspect mode")
    device_context = read("src/ui/DeviceContext.cpp")
    corrected = between(device_context, "void idDeviceContext::GetAspectCorrectedScreenExpansion", "\n}\n")
    require(corrected, "windowWidth, windowHeight, true, xExpand, yExpand", "aspect-corrected expansion helper")

    # A menu GUI's cursor starts at the canvas's top-left corner, not at 0,0 of
    # the centred 4:3 virtual screen, until something places it.
    require(header, "bool						cursorPlaced = false;", "GUI cursor placement flag")
    require(set_cursor, "cursorPlaced = true;", "GUI absolute cursor placement")
    activate = between(source, "const char *idUserInterfaceLocal::Activate(", "void idUserInterfaceLocal::Trigger(")
    for token in (
        "activate && !cursorPlaced && desktop != NULL && ( desktop->GetFlags() & WIN_MENUGUI )",
        "GetCursorBounds( minX, minY, maxX, maxY );",
        "SetCursor( minX, minY );",
    ):
        require(activate, token, "menu cursor canvas-origin start")

    demo_restore = between(source, "void idUserInterfaceLocal::ReadFromDemoFile", "void idUserInterfaceLocal::WriteToDemoFile")
    require(demo_restore, "SetCursor( restoredCursorX, restoredCursorY );", "GUI demo cursor restore")
    save_restore = between(source, "bool idUserInterfaceLocal::ReadFromSaveGame", "void idUserInterfaceLocal::SetKeyBindingNames")
    require(save_restore, "SetCursor( restoredCursorX, restoredCursorY );", "GUI save cursor restore")
    if save_restore.index("desktop->ReadFromSaveGame( savefile );") > save_restore.index(
        "SetCursor( restoredCursorX, restoredCursorY );"
    ):
        raise AssertionError("GUI save cursor is clamped before its restored desktop bounds are available")
    if "cursorX = restoredCursorX" in save_restore or "cursorY = restoredCursorY" in save_restore:
        raise AssertionError("GUI save cursor restore bypasses normalization")

    for token in (
        "idUserInterfaceLocal::WriteToSaveGame: refusing non-finite %s",
        "idUserInterfaceLocal::ReadFromSaveGame: non-finite %s at offset %d",
    ):
        require(source, token, "top-level GUI finite-state validation")
    for token in (
        "refusing non-finite %s",
        "non-finite %s at offset %d",
        "value = 0.0f;",
    ):
        require(winvar, token, "GUI window-variable finite-state validation")

    validation = read("tools/validation/openq4_validate.py")
    require(validation, "ui_cursor_state_safety.py", "GUI cursor regression wiring")
    print("GUI cursor state safety: ok")


if __name__ == "__main__":
    main()
