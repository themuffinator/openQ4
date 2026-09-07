#!/usr/bin/env python3
"""Guards graphical keyboard, mouse, and controller binding presentation.

Binding names are carried through GUI strings as compact ``^ikHH`` tokens,
where ``HH`` is an id key number.  The device context measures and draws those
tokens procedurally so stock menus and HUDs need no replacement materials.
This check keeps formatting, rendering, wrapping, and the two principal
consumers (bindDef rows and spectator hints) on the same contract.
"""

from __future__ import annotations

import os
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(
    os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT.parent / "openQ4-game")
).resolve()


def read(path: Path) -> str:
    if not path.is_file():
        raise AssertionError(f"Required source file not found: {path}")
    return path.read_text(encoding="utf-8", errors="replace")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def body_of(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing {signature!r} in {context}")

    brace_start = source.find("{", start + len(signature))
    if brace_start < 0:
        raise AssertionError(f"Missing body for {signature!r} in {context}")

    depth = 0
    for index in range(brace_start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[brace_start : index + 1]

    raise AssertionError(f"Unterminated body for {signature!r} in {context}")


def validate_binding_formatter() -> None:
    header = read(ROOT / "src" / "framework" / "KeyInput.h")
    source = read(ROOT / "src" / "framework" / "KeyInput.cpp")
    require(header, "KeysFromBindingForMenu", "bounded menu formatter declaration")

    require(
        source,
        '{"RIGHTALT",\t\tK_RIGHT_ALT,\t\t"#str_107651"}',
        "distinct localized Right Alt keycap label",
    )

    controller_classifier = body_of(source, "static bool Key_IsControllerKey(", "KeyInput.cpp")
    require(controller_classifier, '"JOY"', "controller binding classification")
    require(controller_classifier, '"AUX"', "generic-controller binding classification")
    mouse_classifier = body_of(source, "static bool Key_IsMouseKey(", "KeyInput.cpp")
    require(mouse_classifier, "K_MOUSE1", "mouse binding range")
    require(mouse_classifier, "K_MWHEELUP", "mouse-wheel binding range")

    buffers = body_of(source, "static idStr &Key_NextBindingPresentationBuffer(", "KeyInput.cpp")
    match = re.search(r"static\s+idStr\s+buffers\s*\[\s*(\d+)\s*\]", buffers)
    if match is None or int(match.group(1)) < 2:
        raise AssertionError(
            "Binding presentation needs rotating dynamic idStr buffers so adjacent HUD requests remain independent"
        )
    require(buffers, "bufferIndex++", "binding-presentation buffer rotation")
    require(buffers, "result.Clear()", "binding-presentation buffer reset")
    reject(buffers, "char buffers", "dynamic bounded-menu storage")

    append = body_of(source, "static void Key_AppendBindingPresentation(", "KeyInput.cpp")
    require(append, '"#str_07183"', "localized multiple-binding separator")
    require(append, '"^i%c%02x"', "graphical binding token encoding")
    require(append, "emphasized ? 'K' : 'k'", "normal/emphasized binding token subtype")

    compact = body_of(source, "static const char *Key_KeysFromBinding(", "KeyInput.cpp")
    require(compact, "BindingsEquivalent", "compact binding command matching")
    require(compact, "idKeyInput::GetBinding( i )", "shared compact binding scan")
    require(compact, "firstKeyboard", "compact keyboard representative")
    require(compact, "firstMouse", "compact mouse representative")
    require(compact, "firstController", "compact controller representative")
    require(compact, "int selected = -1", "single compact representative")
    require(compact, "key_lastInputFamily == KEY_INPUT_FAMILY_CONTROLLER", "active-family filter")
    require(compact, "selected = firstController", "controller-family representative")
    require(compact, "selected = firstMouse >= 0 ? firstMouse : firstKeyboard", "desktop-family representative")
    require(compact, "if ( selected < 0 )", "other-family fallback")
    require(compact, "Key_AppendBindingPresentation( keyName, selected, emphasized )", "single compact token emission")
    require(compact, '"#str_07133"', "compact localized unbound fallback")
    if compact.count("Key_AppendBindingPresentation(") != 1:
        raise AssertionError("Compact HUD formatting must emit exactly one representative binding")

    compact_entry = body_of(source, "const char *idKeyInput::KeysFromBinding(", "KeyInput.cpp")
    require(compact_entry, "Key_KeysFromBinding( bind, false )", "normal compact binding entry point")
    prompt_entry = body_of(source, "const char *idKeyInput::KeysFromBindingForPrompt(", "KeyInput.cpp")
    require(prompt_entry, "Key_KeysFromBinding( bind, true )", "emphasized center-prompt entry point")

    menu = body_of(source, "const char *idKeyInput::KeysFromBindingForMenu(", "KeyInput.cpp")
    require(menu, "BindingsEquivalent", "bounded menu command matching")
    require(menu, "static const int MAX_MENU_BINDINGS = 6", "six-key menu presentation cap")
    require(menu, "int shown = 0", "shown menu-binding count")
    require(menu, "int omitted = 0", "omitted menu-binding count")
    require(menu, "for ( int i = 0; i < MAX_KEYS; i++ )", "full menu binding/count scan")
    require(menu, "if ( shown < MAX_MENU_BINDINGS )", "graphical-token cap gate")
    require(menu, "Key_AppendBindingPresentation( keyName, i )", "capped menu token emission")
    require(menu, "shown++", "shown menu-binding accounting")
    require(menu, "omitted++", "omitted menu-binding accounting")
    require(menu, "if ( omitted > 0 )", "omitted-count suffix gate")
    require(menu, 'keyName.Append( va( " +%d", omitted ) )', "honest omitted-count suffix")
    require(menu, '"#str_07133"', "bounded menu localized unbound fallback")
    reject(menu, "key_lastInputFamily", "device-independent bounded menu formatting")

    if "idStr::ToLower( keyName )" in compact or "idStr::ToLower( keyName )" in menu:
        raise AssertionError("KeysFromBinding must not lowercase labels carried by graphical tokens")


def validate_active_input_tracking() -> None:
    header = read(ROOT / "src" / "framework" / "KeyInput.h")
    key_input = read(ROOT / "src" / "framework" / "KeyInput.cpp")
    event_loop = read(ROOT / "src" / "framework" / "EventLoop.cpp")
    sdl = read(ROOT / "src" / "sys" / "sdl3" / "sdl3_backend.cpp")

    for declaration in ("PreliminaryMouseEvent", "PreliminaryJoystickEvent"):
        require(header, declaration, "main-event active-input API")

    process = body_of(event_loop, "void idEventLoop::ProcessEvent(", "EventLoop.cpp")
    for token in (
        "SE_KEY",
        "PreliminaryKeyEvent",
        "SE_MOUSE",
        "PreliminaryMouseEvent",
        "SE_JOYSTICK_AXIS",
        "PreliminaryJoystickEvent",
    ):
        require(process, token, "main-event active-input tracking")

    key_event = body_of(key_input, "void idKeyInput::PreliminaryKeyEvent(", "KeyInput.cpp")
    require(key_event, "if ( down )", "key-down-only active-family switch")
    require(key_event, "Key_IsControllerKey( keynum )", "key family classification")
    require(key_event, "KEY_INPUT_FAMILY_CONTROLLER", "controller key provenance")
    require(key_event, "KEY_INPUT_FAMILY_DESKTOP", "desktop key provenance")

    mouse_event = body_of(key_input, "void idKeyInput::PreliminaryMouseEvent(", "KeyInput.cpp")
    require(mouse_event, "deltaX != 0 || deltaY != 0", "nonzero mouse activity gate")
    require(mouse_event, "KEY_INPUT_FAMILY_DESKTOP", "mouse desktop provenance")

    joystick_event = body_of(key_input, "void idKeyInput::PreliminaryJoystickEvent(", "KeyInput.cpp")
    require(joystick_event, "idMath::Abs( value ) >= 16", "post-deadzone joystick activity gate")
    require(joystick_event, "KEY_INPUT_FAMILY_CONTROLLER", "joystick controller provenance")

    init = body_of(key_input, "void idKeyInput::Init(", "KeyInput.cpp")
    require(init, "key_lastInputFamily = KEY_INPUT_FAMILY_DESKTOP", "deterministic startup input family")

    queue_activity = body_of(sdl, "static void SDL3_QueueControllerActivity(", "sdl3_backend.cpp")
    require(queue_activity, "idMath::Abs(value) >= 16", "SDL post-deadzone activity gate")
    require(queue_activity, "SE_JOYSTICK_AXIS", "SDL main-event controller marker")

    for signature, context in (
        ("static void SDL3_UpdateGamepadAxes(", "normalized gamepad axes"),
        ("static void SDL3_UpdateJoystickAxes(", "normalized joystick axes"),
    ):
        axes = body_of(sdl, signature, "sdl3_backend.cpp")
        require(axes, "const int activity = Max", context)
        require(axes, "SDL3_QueueControllerActivity", context)

    for signature, context in (
        ("static void SDL3_HandleGamepadGyroEvent(", "gamepad gyro mouse emulation"),
        ("static void SDL3_HandleGamepadTouchpadEvent(", "gamepad touchpad mouse emulation"),
    ):
        emulation = body_of(sdl, signature, "sdl3_backend.cpp")
        mouse_position = emulation.find("SDL3_QueueMouseDelta")
        controller_position = emulation.find("SDL3_QueueControllerActivity")
        if mouse_position < 0 or controller_position <= mouse_position:
            raise AssertionError(
                f"{context} must restore controller provenance after its synthetic SE_MOUSE event"
            )


def validate_procedural_device_presentation() -> None:
    header = read(ROOT / "src" / "ui" / "DeviceContext.h")
    source = read(ROOT / "src" / "ui" / "DeviceContext.cpp")

    for declaration in (
        "GetKeyBindingIconHeight",
        "GetKeyBindingIconWidth",
        "DrawKeyBindingIcon",
    ):
        require(header, declaration, "DeviceContext graphical binding interface")

    decoder = body_of(source, "static bool openQ4_ExtractKeyBindingIcon(", "DeviceContext.cpp")
    require(decoder, "openQ4_ExtractIconCode", "graphical binding token decoder")
    require(decoder, "code[0] != 'k'", "reserved graphical binding token subtype")
    require(decoder, "openQ4_HexDigitValue", "graphical binding hexadecimal decoder")
    require(decoder, "0xff", "graphical binding eight-bit key-number bounds")
    require(decoder, "Q4_KEY_BINDING_PROMPT_HEIGHT_RATIO", "emphasized center-prompt height decoding")
    require(decoder, "Q4_KEY_BINDING_INLINE_HEIGHT_RATIO", "normal inline height decoding")

    register = body_of(source, "void idDeviceContext::RegisterIcon(", "DeviceContext.cpp")
    guard_high = register.find("code[1] != '\\0'")
    guard_low = register.find("code[2] != '\\0'")
    decode_high = register.find("openQ4_HexDigitValue( code[1] )")
    decode_low = register.find("openQ4_HexDigitValue( code[2] )")
    if not (0 <= guard_high < guard_low < decode_high < decode_low):
        raise AssertionError(
            "RegisterIcon must prove both kHH payload bytes exist before decoding the reserved code"
        )
    require(register, "code == NULL", "registered-icon null-code guard")
    if register.find("code == NULL") > register.find("code[0] == '\\0'"):
        raise AssertionError("RegisterIcon must reject a null code before reading its first byte")
    require(register, "reserved for graphical key bindings", "reserved kHH registration rejection")

    info = body_of(source, "static void openQ4_GetKeyBindingIconInfo(", "DeviceContext.cpp")
    for token in (
        "Q4_KEY_BINDING_KEYBOARD",
        "Q4_KEY_BINDING_MOUSE_BUTTON",
        "Q4_KEY_BINDING_MOUSE_WHEEL",
        "Q4_KEY_BINDING_PAD_FACE",
        "Q4_KEY_BINDING_PAD_SHOULDER",
        "Q4_KEY_BINDING_PAD_DPAD",
        "Q4_KEY_BINDING_PAD_STICK",
        "Q4_KEY_BINDING_PAD_MENU",
        "Q4_KEY_BINDING_PAD_GENERIC",
        "K_MOUSE1",
        "K_MOUSE8",
        "K_MWHEELUP",
        "K_MWHEELDOWN",
        '"JOY"',
        '"AUX"',
        "KeyNumToString( keyNum, true )",
    ):
        require(info, token, "keyboard/mouse/controller binding classification")

    arrow_key = body_of(source, "static bool openQ4_IsKeyboardArrowKey(", "DeviceContext.cpp")
    for token in ("K_UPARROW", "K_DOWNARROW", "K_LEFTARROW", "K_RIGHTARROW"):
        require(arrow_key, token, "directional keyboard-key classification")
    arrow_classification = info[: info.find("if ( keyNum >= K_MOUSE1")]
    require(arrow_classification, "openQ4_IsKeyboardArrowKey", "arrow-key presentation override")
    require(arrow_classification, "info.label.Clear()", "word-free arrow-key legend")
    mouse_classification = info[info.find("if ( keyNum >= K_MOUSE1") :]
    mouse_classification = mouse_classification[: mouse_classification.find("if ( keyNum == K_MWHEELUP")]
    require(mouse_classification, "info.label.Clear()", "word-free mouse-button presentation")
    reject(mouse_classification, 'va( "%d"', "ambiguous numbered mouse-button presentation")
    require(info, 'GetString( "#str_200018" )', "localized controller Back keycap")

    height = body_of(source, "float idDeviceContext::GetKeyBindingIconHeight(", "DeviceContext.cpp")
    require(height, "MaxCharHeight( textScale )", "font-relative keycap height")
    require(height, "heightRatio = idMath::ClampFloat( 0.50f, 2.00f, heightRatio )", "bounded keycap height ratio")
    require(height, "heightRatio / Q4_KEY_BINDING_INLINE_HEIGHT_RATIO", "scaled minimum keycap height")
    require(height, "lineHeight * heightRatio", "requested line-relative keycap height")
    require(source, "Q4_KEY_BINDING_INLINE_HEIGHT_RATIO = 0.99f", "near-line-height inline keycaps")
    require(source, "Q4_KEY_BINDING_PROMPT_HEIGHT_RATIO = 1.50f", "150-percent center-prompt keycaps")

    width = body_of(source, "float idDeviceContext::GetKeyBindingIconWidth(", "DeviceContext.cpp")
    for token in (
        "Q4_KEY_BINDING_KEYBOARD",
        "float widthClass",
        "info.label.Length() > 1 ? 1.34f : 1.0f",
        "K_SPACE",
        "K_ENTER",
        "K_BACKSPACE",
        "K_SHIFT",
        "K_TAB",
        "K_CTRL",
        "Q4_KEY_BINDING_MOUSE_BUTTON",
        "width = height * 0.74f",
        "width = height * 0.98f",
        "Q4_KEY_BINDING_PAD_FACE",
        "Q4_KEY_BINDING_PAD_SHOULDER",
        "Q4_KEY_BINDING_PAD_STICK",
        "Q4_KEY_BINDING_PAD_TOUCHPAD",
        "return idMath::Ceil( width )",
    ):
        require(width, token, "bounded semantic keycap sizing")

    label_growth = body_of(
        width,
        "if ( info.label.Length() > 0 &&",
        "bounded keycap label growth",
    )
    require(
        width,
        "info.kind == Q4_KEY_BINDING_KEYBOARD || info.kind == Q4_KEY_BINDING_PAD_MENU || info.kind == Q4_KEY_BINDING_PAD_GENERIC",
        "keyboard/menu/generic label-aware keycap growth",
    )
    for token in (
        "TextWidth( info.label, labelScale",
        "const float maximumWidth",
        "info.kind == Q4_KEY_BINDING_PAD_GENERIC ? 1.90f : 2.20f",
        "width = Min( maximumWidth, Max( width, labelWidth + sidePadding * 2.0f ) )",
        "SetFontByScale( textScale )",
    ):
        require(label_growth, token, "bounded semantic label-aware keycap growth")
    if width.count("TextWidth( info.label") != 1:
        raise AssertionError("Keycap measurement must perform one bounded label-growth measurement")

    require(info, "case 3: case 4: case 5: case 6:", "four positional controller face buttons")
    face_classification = info[info.find("case 3: case 4: case 5: case 6:") :]
    face_classification = face_classification[: face_classification.find("case 7:")]
    require(face_classification, "Q4_KEY_BINDING_PAD_FACE", "controller face-button classification")
    require(face_classification, "info.label.Clear()", "controller-neutral face-button legend")
    for legend in ("A", "B", "X", "Y", "LB", "RB", "LT", "RT"):
        reject(info, f'info.label = "{legend}"', "controller-neutral classification")

    draw = body_of(source, "void idDeviceContext::DrawKeyBindingIcon(", "DeviceContext.cpp")
    require(draw, "openQ4_DrawKeycapBase", "procedural keyboard keycap")
    require(draw, "openQ4_DrawSmoothRoundedFill", "high-sample smooth device shapes")
    require(draw, "Q4_KEY_BINDING_MOUSE_BUTTON", "procedural mouse presentation")
    require(draw, "Q4_KEY_BINDING_MOUSE_WHEEL", "procedural mouse-wheel presentation")
    require(draw, "Q4_KEY_BINDING_PAD_FACE", "controller face-button presentation")
    require(draw, "Q4_KEY_BINDING_PAD_DPAD", "controller D-pad presentation")
    require(draw, "Q4_KEY_BINDING_PAD_STICK", "controller stick presentation")
    require(draw, "DrawFilledRect", "procedural device shapes")
    require(draw, "TextWidth( info.label", "key label centering")
    require(draw, "availableWidth", "key label fit within bounded keycap")
    require(draw, "openQ4_FontInkExtents", "vertically centered key label")
    require(draw, "DrawText( labelX", "text over procedural keycap")

    arrow_draw = body_of(source, "static void openQ4_DrawKeyboardArrowGlyph(", "DeviceContext.cpp")
    for token in ("const int bands = 6", "shaftThickness", "headWidth", "DrawFilledRect"):
        require(arrow_draw, token, "procedural directional arrow character")
    require(draw, "openQ4_DrawKeyboardArrowGlyph", "square keyboard arrow-key rendering")

    label_draw_position = draw.rfind("if ( info.label.Length() > 0 )")
    if label_draw_position < 0:
        raise AssertionError("Missing final keycap label drawing block")
    label_draw = draw[label_draw_position:]
    label_x_position = label_draw.find("const float labelX")
    if label_x_position < 0:
        raise AssertionError("Keycap label draw must center its final fitted measurement")
    exact_fit = label_draw[:label_x_position]
    require(
        exact_fit,
        "labelScale *= availableWidth / labelWidth;",
        "exact final keycap label fit",
    )
    if exact_fit.count("TextWidth( info.label") != 2:
        raise AssertionError("Exact keycap label fit must remeasure once after scaling")
    for floor in ("labelScale = Max(", "Max( textScale"):
        reject(exact_fit, floor, "floor-free exact keycap label fit")
    reject(label_draw, "mouseButtonLabel", "text-free mouse-button drawing")

    mouse_draw = body_of(draw, "case Q4_KEY_BINDING_MOUSE_BUTTON:", "mouse drawing")
    for token in (
        "const float mouseWidth = height * 0.68f",
        "const float splitY = y + height * 0.46f",
        "info.detail == 1 ? selectedButton : midFace",
        "info.detail == 2 ? selectedButton : midFace",
        "info.detail == 3",
        "const int arrowBands = 4",
        "selectedSideButton",
    ):
        require(mouse_draw, token, "upright physical mouse-button presentation")

    face_draw = body_of(draw, "case Q4_KEY_BINDING_PAD_FACE:", "controller face-button drawing")
    for token in (
        "buttonX[4]",
        "buttonY[4]",
        "selectedButton",
        "info.detail - 3",
        "i == selectedButton ? accent : inactiveFace",
        "buttonSize * 0.5f",
    ):
        require(face_draw, token, "neutral positional controller face-button cluster")
    require(
        draw,
        "const idVec4 inactiveFace = openQ4_KeyBindingColor( 0.30f, 0.36f, 0.42f, 0.90f, color.w )",
        "legible neutral controller face-button contrast",
    )
    reject(face_draw, "DrawText", "controller-neutral face-button legend")
    reject(face_draw, "openQ4_KeyBindingColor", "controller-neutral face-button colour mapping")

    rounded = body_of(source, "static void openQ4_DrawRoundedFillCore(", "DeviceContext.cpp")
    for token in (
        "Q4_KEY_BINDING_MAX_CURVE_BANDS",
        "radius * 6.0f",
        "idMath::Sqrt",
        "bandHeight",
    ):
        require(rounded, token, "high-resolution procedural curve sampling")
    smooth = body_of(source, "static void openQ4_DrawSmoothRoundedFill(", "DeviceContext.cpp")
    require(smooth, "edgeColor.w *= 0.38f", "soft procedural edge fringe")
    require(smooth, "openQ4_DrawRoundedFillCore", "smooth rounded-fill composition")


def validate_draw_and_measure_pipeline() -> None:
    source = read(ROOT / "src" / "ui" / "DeviceContext.cpp")

    repeat_escape = body_of(
        source,
        "static bool openQ4_IsRepeatTextEscape(",
        "DeviceContext.cpp",
    )
    require(repeat_escape, "escapeLength > 2", "complete repeat-wrapper recognition")
    require(repeat_escape, "escape[1] == 'N'", "uppercase repeat-wrapper recognition")
    require(repeat_escape, "escape[1] == 'n'", "lowercase repeat-wrapper recognition")

    resolver = body_of(source, "static void openQ4_ResolveTextEscape(", "DeviceContext.cpp")
    for token in (
        "openQ4_IsRepeatTextEscape",
        "repeatedPayload = source + escapeLength",
        "openQ4_TextEscapeLength( repeatedPayload",
        "payload = repeatedPayload",
        "payloadLength = repeatedLength",
        "payloadType = repeatedType",
        "sourceLength += repeatedLength",
        "repeats = openQ4_TextEscapeRepeatCount( source )",
    ):
        require(resolver, token, "repeat-wrapper payload resolution")

    draw = body_of(source, "int idDeviceContext::DrawText(float x,", "DeviceContext.cpp")
    require(draw, "openQ4_ExtractKeyBindingIcon", "low-level text draw")
    require(draw, "DrawKeyBindingIcon", "low-level text draw")
    require(draw, "GetKeyBindingIconWidth", "low-level text draw advance")
    if draw.find("openQ4_ExtractKeyBindingIcon") > draw.find("FindIcon("):
        raise AssertionError("Graphical binding tokens must be decoded before ordinary registered-icon lookup")
    for token in (
        "const char *backgroundScan = text",
        "openQ4_ResolveTextEscape( backgroundScan",
        "scanRepeats > 0 && scanPayloadType == S_ESCAPE_ICON",
        "openQ4_ExtractKeyBindingIcon( scanPayload",
        "backgroundScan += scanSourceLength",
        "backgroundIndex += scanSourceLength",
    ):
        require(draw, token, "repeat-aware accessibility keycap scan")

    width = body_of(source, "int idDeviceContext::TextWidth(", "DeviceContext.cpp")
    for token in (
        "openQ4_ResolveTextEscape",
        "payloadType == S_ESCAPE_ICON && repeats > 0",
        "openQ4_ExtractKeyBindingIcon( payload",
        "GetKeyBindingIconWidth( keyNum, scale, bindingHeightRatio ) * repeats",
        "s += sourceLength",
        "index += sourceLength",
    ):
        require(width, token, "repeat-aware TextWidth binding measurement")

    height = body_of(source, "int idDeviceContext::TextHeight(", "DeviceContext.cpp")
    for token in (
        "openQ4_ResolveTextEscape",
        "payloadType == S_ESCAPE_ICON && repeats > 0",
        "openQ4_ExtractKeyBindingIcon( payload",
        "GetKeyBindingIconHeight( scale, bindingHeightRatio )",
        "s += sourceLength",
        "index += sourceLength",
    ):
        require(height, token, "repeat-aware TextHeight binding measurement")

    max_index = body_of(source, "bool idDeviceContext::GetMaxTextIndex(", "DeviceContext.cpp")
    for token in (
        "openQ4_ResolveTextEscape",
        # A character is one to four UTF-8 bytes, and maxIndex is consumed as a
        # string length, so a plain 1 here would hand the caller a truncated
        # lead byte on any non-ASCII character.
        "const int tokenLength = escapeLength > 0 ? sourceLength : Max( 1, next.bytes )",
        "payloadType == S_ESCAPE_ICON && repeats > 0",
        "openQ4_ExtractKeyBindingIcon( payload",
        "bindingWidth * repeats / useScale",
    ):
        require(max_index, token, "repeat-aware atomic line-fit measurement")
    require(
        max_index,
        "wrapInfo.maxIndex = index > 0 ? index : tokenLength;",
        "atomic repeat-plus-icon wrap boundary and leading-token progress",
    )
    if "wrapInfo.maxIndex = lastTokenIndex - 1" in max_index:
        raise AssertionError("Line fitting must never split inside a graphical binding token")

    wrapped_draw = body_of(
        source,
        "int idDeviceContext::DrawText( const char *text,",
        "DeviceContext.cpp",
    )
    for token in (
        "char buff[Q4_TEXT_LINE_BUFFER_SIZE]",
        "openQ4_ResolveTextEscape( p",
        "escapePayloadType == S_ESCAPE_ICON",
        "len + escapeSourceLength < static_cast<int>( sizeof( buff ) )",
        "idStr::Copynz( &buff[len], p, escapeSourceLength + 1 )",
        "openQ4_ExtractKeyBindingIcon( escapePayload",
        "GetKeyBindingIconWidth( keyNum, textScale, bindingHeightRatio ) * escapeRepeats",
        "escapeRepeats * openQ4_ScaledFontUnits",
        "!( isIconEscape && len == 0 )",
        "len += escapeSourceLength",
        "p += escapeSourceLength",
    ):
        require(wrapped_draw, token, "repeat-aware atomic chat lookahead")
    require(source, "static const int Q4_TEXT_LINE_BUFFER_SIZE = 1024", "fixed wrapped-text line buffer")
    require(
        wrapped_draw,
        "const float lineSkip = Max( fontLineSkip, contentHeight )",
        "emphasized prompt-aware line height",
    )


def validate_bind_widget_fit_and_capture() -> None:
    source = read(ROOT / "src" / "ui" / "BindWindow.cpp")
    handle = body_of(source, "const char *idBindWindow::HandleEvent(", "BindWindow.cpp")

    cancel_signature = "if (key == K_ESCAPE || key == K_JOY7 || key == K_JOY8)"
    capture_clear_signature = "} else if (key == K_BACKSPACE || key == K_DEL)"
    focused_clear_signature = "} else if (key == K_BACKSPACE || key == K_DEL || key == K_JOY6)"
    cancel_position = handle.find(cancel_signature)
    capture_clear_position = handle.find(capture_clear_signature)
    focused_clear_position = handle.find(focused_clear_signature)
    if not (0 <= cancel_position < capture_clear_position < focused_clear_position):
        raise AssertionError("Bind capture must keep cancel, capture-clear, and focused-row clear actions distinct")

    require(handle[:cancel_position], "waitingOnKey = false", "capture exit before cancel/clear handling")
    cancel_branch = handle[cancel_position:capture_clear_position]
    require(cancel_branch, 'return ""', "capture cancel without a command")
    require(cancel_branch, "*updateVisuals = true", "capture cancel visual refresh")
    reject(cancel_branch, "clearbind", "capture cancel preserving the existing binding")

    capture_bind_position = handle.find("} else {", capture_clear_position + len(capture_clear_signature))
    if capture_bind_position < 0 or capture_bind_position >= focused_clear_position:
        raise AssertionError("Could not isolate the in-capture clear action")
    capture_clear_branch = handle[capture_clear_position:capture_bind_position]
    require(capture_clear_branch, "clearbind", "Backspace/Delete clear during capture")
    reject(handle[:focused_clear_position], "K_JOY6", "west face button remains bindable during capture")

    focused_clear_branch = handle[focused_clear_position:]
    require(focused_clear_branch, "clearbind", "focused-row keyboard/controller clear action")
    require(focused_clear_branch, "*updateVisuals = true", "focused-row clear visual refresh")
    if handle.count("clearbind") != 2:
        raise AssertionError("BindWindow must issue clearbind only for capture-clear and focused-row clear")

    draw = body_of(source, "void idBindWindow::Draw(", "BindWindow.cpp")
    for token in (
        'str.Find( "^ik" ) >= 0',
        "dc->TextWidth( str, drawScale",
        "naturalWidth > textRect.w",
        "textScale * textRect.w",
        "Max( textScale * 0.72f",
        "dc->DrawText( str, drawScale",
    ):
        require(draw, token, "bounded menu-binding row fitting")
    require(draw, "!waitingOnKey", "capture prompt remains at authored text scale")

    controls = read(
        ROOT / "content" / "baseoq4" / "pak0" / "guis" / "menu" / "settings" / "controls.gui"
    )
    bind_defs = list(re.finditer(r"\bbindDef\s+([A-Za-z0-9_]+)", controls))
    if len(bind_defs) != 48:
        raise AssertionError(f"Expected 48 stock controls bindDef rows, found {len(bind_defs)}")

    number = r"(?:\d+(?:\.\d*)?|\.\d+)"
    rect_pattern = re.compile(
        rf"\brect\s+(-?{number}),\s*(-?{number}),\s*(-?{number}),\s*(-?{number})"
    )
    scale_pattern = re.compile(rf"\btextscale\s+({number})")
    for bind_def in bind_defs:
        name = bind_def.group(1)
        block = body_of(controls, bind_def.group(0), f"controls.gui bindDef {name}")
        rect = rect_pattern.search(block)
        scale = scale_pattern.search(block)
        if rect is None or scale is None:
            raise AssertionError(f"Missing rect/textscale in controls bindDef {name}")
        width = float(rect.group(3))
        height = float(rect.group(4))
        text_scale = float(scale.group(1))
        if width != 149.0 or height not in (17.0, 18.0) or text_scale != 0.24:
            raise AssertionError(
                f"controls bindDef {name} must retain a fitted 149x17/18 row at textscale 0.24"
            )


def validate_localized_controls_hint() -> None:
    controls = read(
        ROOT / "content" / "baseoq4" / "pak0" / "guis" / "menu" / "settings" / "controls.gui"
    )
    require(controls, 'text\t"#str_200930"', "localized controls hint consumer")

    strings_root = ROOT / "content" / "baseoq4" / "pak0" / "strings"
    languages = ("english", "french", "italian", "spanish")
    expected_pairs = (
        ("^ik0d", "^ikc7"),
        ("^ik90", "^ikca"),
        ("^ik1b", "^ikcc"),
    )
    entry_pattern = re.compile(r'^\s*"#str_200930"\s*"([^"\r\n]*)"\s*$', re.MULTILINE)
    back_entry_pattern = re.compile(r'^\s*"#str_200018"\s*"([^"\r\n]+)"\s*$', re.MULTILINE)
    token_pattern = re.compile(r"\^ik[0-9a-fA-F]{2}")

    for language in languages:
        path = strings_root / f"{language}_guis.lang"
        matches = entry_pattern.findall(read(path))
        if len(matches) != 1:
            raise AssertionError(f"Expected one #str_200930 entry in {path}")
        lines = matches[0].split(r"\n")
        if len(lines) != 3:
            raise AssertionError(f"#str_200930 in {path} must have three explicit GUI lines")
        for line_number, (line, expected) in enumerate(zip(lines, expected_pairs), start=1):
            tokens = tuple(token.lower() for token in token_pattern.findall(line))
            if tokens != expected:
                raise AssertionError(
                    f"#str_200930 line {line_number} in {path} must use {expected}, found {tokens}"
                )
            if line.count("/") != 1:
                raise AssertionError(f"#str_200930 line {line_number} in {path} needs one input separator")
            label = token_pattern.sub("", line).replace("/", "").strip()
            if not label:
                raise AssertionError(f"#str_200930 line {line_number} in {path} needs a localized action label")
        back_matches = back_entry_pattern.findall(read(path))
        if len(back_matches) != 1 or not back_matches[0].strip():
            raise AssertionError(f"Expected one localized controller Back label in {path}")


def validate_bind_menu_and_spectator_consumers() -> None:
    user_interface = read(ROOT / "src" / "ui" / "UserInterface.cpp")
    recurse = body_of(
        user_interface,
        "void idUserInterfaceLocal::RecurseSetKeyBindingNames(",
        "UserInterface.cpp",
    )
    require(
        recurse,
        "idKeyInput::KeysFromBindingForMenu(",
        "bounded-menu bindDef GUI-state population",
    )
    if re.search(r"idKeyInput::KeysFromBinding\s*\(", recurse):
        raise AssertionError("bindDef GUI-state population must use the bounded menu formatter")

    bind_window = read(ROOT / "src" / "ui" / "BindWindow.cpp")
    bind_draw = body_of(bind_window, "void idBindWindow::Draw(", "BindWindow.cpp")
    require(bind_draw, "bindName.c_str()", "bindDef graphical value selection")
    require(bind_draw, "dc->DrawText", "bindDef graphical value rendering")

    mphud = read(ROOT / "content" / "baseoq4" / "pak0" / "guis" / "mphud.gui")
    require(mphud, '"gui::spectatetext1"', "multiplayer spectator HUD binding prompt")
    ready_prompt_window = body_of(mphud, "windowDef Spectate1", "mphud.gui Spectate1")
    if re.search(r"\brect\s+0\s*,\s*141\s*,\s*640\s*,\s*40\b", ready_prompt_window) is None:
        raise AssertionError("The stock non-tourney readiness prompt must retain its 640x40 HUD window")
    require(ready_prompt_window, "textscale\t0.31", "stock readiness prompt text scale")

    code_strings = read(
        ROOT / "content" / "baseoq4" / "pak0" / "strings" / "english_code.lang"
    )
    for localized_string in ("#str_107710", "#str_107711"):
        if re.search(
            rf'^\s*"{localized_string}"\s*"[^"\r\n]*\\nPress %s to [^"\r\n]+"\s*$',
            code_strings,
            re.MULTILINE,
        ) is None:
            raise AssertionError(f"{localized_string} must retain its explicit two-line readiness layout")

    checked_tree = False
    for tree in ("mpgame", "game"):
        multiplayer = GAME_LIBS_ROOT / "src" / tree / "MultiplayerGame.cpp"
        if not multiplayer.is_file():
            continue
        checked_tree = True
        multiplayer_source = read(multiplayer)
        update_hud = body_of(multiplayer_source, "void idMultiplayerGame::UpdateHud(", str(multiplayer))
        require(update_hud, 'KeysFromBindingForPrompt( "_attack" )', f"{tree} emphasized spectator attack binding")
        require(update_hud, 'KeysFromBindingForPrompt( "_moveup" )', f"{tree} emphasized spectator exit-follow binding")
        require(update_hud, 'KeysFromBindingForPrompt( "_impulse14" )', f"{tree} emphasized previous-player binding")
        require(update_hud, 'KeysFromBindingForPrompt( "_impulse15" )', f"{tree} emphasized next-player binding")
        require(update_hud, 'SetStateString( "spectatetext1"', f"{tree} spectator HUD publication")
        reject(update_hud, 'KeysFromBinding( "_attack" )', f"{tree} non-emphasized spectator attack binding")

        all_ready = body_of(multiplayer_source, "bool idMultiplayerGame::AllPlayersReady(", str(multiplayer))
        for localized_string in ("#str_110017", "#str_110018"):
            require(
                all_ready,
                f'GetLocalizedString( "{localized_string}" ), common->KeysFromBindingForPrompt( "_impulse17" )',
                f"{tree} emphasized single-line tourney readiness prompt",
            )
        for localized_string in ("#str_107710", "#str_107711"):
            require(
                all_ready,
                f'GetLocalizedString( "{localized_string}" ), common->KeysFromBinding( "_impulse17" )',
                f"{tree} inline two-line readiness prompt",
            )
        require(
            all_ready,
            "stock non-tourney HUD fits two normal-height lines",
            f"{tree} stock readiness prompt height rationale",
        )
        start_vote = body_of(multiplayer_source, "void idMultiplayerGame::ClientStartPackedVote(", str(multiplayer))
        require(start_vote, 'KeysFromBindingForPrompt("_impulse28")', f"{tree} emphasized vote-yes prompt")
        require(start_vote, 'KeysFromBindingForPrompt("_impulse29")', f"{tree} emphasized vote-no prompt")

    if not checked_tree:
        raise AssertionError(f"No companion game-library source trees found below {GAME_LIBS_ROOT}")


def validate_ready_binding_contract() -> None:
    default_cfg = read(ROOT / "content" / "baseoq4" / "pak0" / "default.cfg")
    controls = read(
        ROOT
        / "content"
        / "baseoq4"
        / "pak0"
        / "guis"
        / "menu"
        / "settings"
        / "controls.gui"
    )
    if re.search(r"(?m)^\s*bind\s+F3\s+_impulse17(?:\s+//.*)?$", default_cfg) is None:
        raise AssertionError("The shipped F3 ready default must use stock _impulse17")
    for key, impulse in (("F1", "_impulse28"), ("F2", "_impulse29"), ("F6", "_impulse20"), ("F7", "_impulse22")):
        if re.search(rf"(?m)^\s*bind\s+{key}\s+{impulse}(?:\s+//.*)?$", default_cfg) is None:
            raise AssertionError(f"The shipped {key} default must use stock {impulse}")
    if re.search(
        r"bindDef\s+set_ctrls_other_ready_key\s*\{.*?\bbind\s+_impulse17\b",
        controls,
        re.DOTALL,
    ) is None:
        raise AssertionError("The controls-menu ready row must edit stock _impulse17")
    for widget, impulse in (("voteyes", "_impulse28"), ("voteno", "_impulse29")):
        if re.search(
            rf"bindDef\s+set_ctrls_other_{widget}_key\s*\{{.*?\bbind\s+{impulse}\b",
            controls,
            re.DOTALL,
        ) is None:
            raise AssertionError(f"The controls-menu {widget} row must edit stock {impulse}")

    multiplayer_path = GAME_LIBS_ROOT / "src" / "mpgame" / "MultiplayerGame.cpp"
    multiplayer = read(multiplayer_path)
    toggle_ready = body_of(
        multiplayer,
        "void idMultiplayerGame::ToggleReady(",
        str(multiplayer_path),
    )
    require(toggle_ready, "SendReady( !ready );", "reliable impulse-17 ready toggle")
    reject(toggle_ready, "SetCVarString", "userinfo-only impulse-17 ready toggle")

    send_ready = body_of(multiplayer, "void idMultiplayerGame::SendReady(", str(multiplayer_path))
    require(
        send_ready,
        "GAME_RELIABLE_MESSAGE_READY",
        "casual client ready reliable message",
    )
    require(
        send_ready,
        "ServerSetPlayerReady",
        "listen-server authoritative ready path",
    )
    ready_command = body_of(
        multiplayer,
        "void idMultiplayerGame::Ready_f(",
        str(multiplayer_path),
    )
    require(ready_command, "SendReady( true );", "idempotent ready command")

    reset = body_of(multiplayer, "void idMultiplayerGame::Reset(", str(multiplayer_path))
    require(
        reset,
        'common->BindingFromKey( "F3" )',
        "exact legacy F3 ready migration lookup",
    )
    require(
        reset,
        '"bind F3 _impulse17\\n"',
        "exact legacy F3 ready migration update",
    )
    for key, legacy, impulse in (
        ("F1", "voteyes", "_impulse28"),
        ("F2", "voteno", "_impulse29"),
        ("F6", "toggleteam", "_impulse20"),
        ("F7", "spectate", "_impulse22"),
    ):
        require(reset, f'common->BindingFromKey( "{key}" )', f"exact legacy {key} migration lookup")
        require(reset, f'"{legacy}"', f"exact legacy {key} binding match")
        require(reset, f'"bind {key} {impulse}\\n"', f"exact legacy {key} migration update")


def main() -> int:
    try:
        validate_binding_formatter()
        validate_active_input_tracking()
        validate_procedural_device_presentation()
        validate_draw_and_measure_pipeline()
        validate_bind_widget_fit_and_capture()
        validate_localized_controls_hint()
        validate_bind_menu_and_spectator_consumers()
        validate_ready_binding_contract()
    except AssertionError as error:
        print(f"key_bind_presentation: FAILED - {error}")
        return 1

    print("key_bind_presentation: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
