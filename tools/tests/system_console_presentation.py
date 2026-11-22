#!/usr/bin/env python3
"""Cross-platform system console presentation contract.

The Win32 console (src/sys/win32/win_syscon.cpp) and the SDL3 console used by
Linux and macOS (src/sys/posix/posix_syscon.cpp) are separate implementations of
the same product surface. This checks three things:

  1. Both take their palette and metrics from src/sys/sys_console_theme.h rather
     than hard-coding literals, so a colour or measurement cannot drift on one
     platform without changing on all three.
  2. The shared metrics actually fit: replaying the POSIX layout formula at the
     minimum, default and oversized window sizes must produce non-overlapping
     rows that stay inside the client area and leave a usable log pane.
  3. The presentation affordances that make the console legible are present on
     both sides: log line leading, a themed command line, hover/press feedback,
     a scroll indicator and a fatal-error status treatment.
"""

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    depth = 0
    for index in range(start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find end of function {signature!r}")


def parse_theme() -> dict:
    """Every #define in the shared theme header that carries a numeric value."""
    values = {}
    for name, value in re.findall(
        r"^#define\s+(SYSCON_\w+)\s+(0x[0-9a-fA-F]+|\d+)\s*$",
        read("src/sys/sys_console_theme.h"),
        re.M,
    ):
        values[name] = int(value, 0)
    return values


PALETTE_ENTRIES = (
    "WINDOW",
    "PANEL",
    "INPUT",
    "BUTTON",
    "BUTTON_HOT",
    "BUTTON_DOWN",
    "BORDER",
    "BORDER_LIT",
    "TEXT",
    "TEXT_DIM",
    "ALERT",
    "ALERT_PANEL",
    "SCROLL_TRACK",
    "SCROLL_THUMB",
)

METRIC_ENTRIES = (
    "WINDOW_W",
    "WINDOW_H",
    "MIN_W",
    "MIN_H",
    "MARGIN",
    "GUTTER",
    "STATUS_H",
    "INPUT_H",
    "BUTTON_H",
    "BUTTON_W",
    "TEXT_PAD",
    "LINE_H",
    "GLYPH_W",
    "GLYPH_H",
    "SCROLLBAR_W",
    "MONO_PT",
    "UI_PT",
)


def validate_theme_header() -> None:
    theme = parse_theme()

    for entry in PALETTE_ENTRIES:
        for channel in ("R", "G", "B"):
            key = f"SYSCON_{entry}_{channel}"
            if key not in theme:
                raise AssertionError(f"Shared console theme is missing {key}")
            if not 0 <= theme[key] <= 0xFF:
                raise AssertionError(f"{key} is not a byte value: {theme[key]}")

    for entry in METRIC_ENTRIES:
        key = f"SYSCON_METRIC_{entry}"
        if key not in theme:
            raise AssertionError(f"Shared console theme is missing {key}")
        if theme[key] <= 0:
            raise AssertionError(f"{key} must be positive: {theme[key]}")

    source = read("src/sys/sys_console_theme.h")
    require(source, "#define SYSCON_RGB( name )", "shared console palette accessor")
    require(source, "SYSCON_STATUS_READY_TEXT", "shared console ready status text")
    for label in ("SYSCON_LABEL_COPY", "SYSCON_LABEL_CLEAR", "SYSCON_LABEL_QUIT"):
        require(source, label, "shared console button labels")

    # Log lines are drawn on an 8px glyph cell. Advancing by the cell height
    # alone leaves no leading and the rows collide, so the shared pitch has to
    # stay strictly taller than the glyph.
    if theme["SYSCON_METRIC_LINE_H"] <= theme["SYSCON_METRIC_GLYPH_H"]:
        raise AssertionError(
            "SYSCON_METRIC_LINE_H must exceed SYSCON_METRIC_GLYPH_H so log lines have leading"
        )

    if theme["SYSCON_METRIC_MIN_W"] > theme["SYSCON_METRIC_WINDOW_W"]:
        raise AssertionError("Console minimum width exceeds its default width")
    if theme["SYSCON_METRIC_MIN_H"] > theme["SYSCON_METRIC_WINDOW_H"]:
        raise AssertionError("Console minimum height exceeds its default height")


def console_layout(theme: dict, width: int, height: int) -> dict:
    """Replays the row stack both consoles lay out, in design units.

    Mirrors Posix_ConsoleUpdateLayout in src/sys/posix/posix_syscon.cpp and
    SysCon_LayoutChildren in src/sys/win32/win_syscon.cpp.
    """
    width = max(width, theme["SYSCON_METRIC_MIN_W"])
    height = max(height, theme["SYSCON_METRIC_MIN_H"])

    margin = theme["SYSCON_METRIC_MARGIN"]
    gutter = theme["SYSCON_METRIC_GUTTER"]
    button_w = theme["SYSCON_METRIC_BUTTON_W"]
    button_h = theme["SYSCON_METRIC_BUTTON_H"]
    content_w = width - margin * 2

    button_y = height - margin - button_h
    input_y = button_y - gutter - theme["SYSCON_METRIC_INPUT_H"]
    output_y = margin + theme["SYSCON_METRIC_STATUS_H"] + gutter
    output_h = max(theme["SYSCON_METRIC_LINE_H"], input_y - gutter - output_y)

    return {
        "client": (0, 0, width, height),
        "rects": {
            "status": (margin, margin, content_w, theme["SYSCON_METRIC_STATUS_H"]),
            "log": (margin, output_y, content_w, output_h),
            "input": (margin, input_y, content_w, theme["SYSCON_METRIC_INPUT_H"]),
            "copy": (margin, button_y, button_w, button_h),
            "clear": (margin + button_w + gutter, button_y, button_w, button_h),
            "quit": (width - margin - button_w, button_y, button_w, button_h),
        },
    }


def rects_overlap(a: tuple, b: tuple) -> bool:
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    return ax < bx + bw and bx < ax + aw and ay < by + bh and by < ay + ah


def validate_layout_fits() -> None:
    theme = parse_theme()

    sizes = (
        (theme["SYSCON_METRIC_MIN_W"], theme["SYSCON_METRIC_MIN_H"]),
        (theme["SYSCON_METRIC_WINDOW_W"], theme["SYSCON_METRIC_WINDOW_H"]),
        (1920, 1080),
        # Smaller than the enforced minimum: both consoles clamp, so the layout
        # still has to come out valid rather than inverted.
        (120, 90),
    )

    for width, height in sizes:
        layout = console_layout(theme, width, height)
        client_w, client_h = layout["client"][2], layout["client"][3]
        rects = layout["rects"]

        for name, (x, y, w, h) in rects.items():
            if w <= 0 or h <= 0:
                raise AssertionError(
                    f"Console {name!r} collapsed at {width}x{height}: {w}x{h}"
                )
            if x < 0 or y < 0 or x + w > client_w or y + h > client_h:
                raise AssertionError(
                    f"Console {name!r} leaves the client area at {width}x{height}: "
                    f"({x},{y},{w},{h}) in {client_w}x{client_h}"
                )

        names = sorted(rects)
        for i, first in enumerate(names):
            for second in names[i + 1 :]:
                if rects_overlap(rects[first], rects[second]):
                    raise AssertionError(
                        f"Console {first!r} overlaps {second!r} at {width}x{height}: "
                        f"{rects[first]} vs {rects[second]}"
                    )

        # The log pane is the reason the window exists; a layout that leaves it
        # with a couple of lines is a layout that has gone wrong.
        log_h = rects["log"][3]
        usable_lines = (log_h - theme["SYSCON_METRIC_TEXT_PAD"] * 2) // theme["SYSCON_METRIC_LINE_H"]
        if usable_lines < 8:
            raise AssertionError(
                f"Console log pane holds only {usable_lines} lines at {width}x{height}"
            )

        # The log's wrap width has to survive reserving the scroll gutter.
        log_text_w = (
            rects["log"][2]
            - theme["SYSCON_METRIC_TEXT_PAD"] * 2
            - theme["SYSCON_METRIC_SCROLLBAR_W"]
        )
        columns = log_text_w // theme["SYSCON_METRIC_GLYPH_W"]
        if columns < 40:
            raise AssertionError(
                f"Console log pane wraps at {columns} columns at {width}x{height}"
            )


def validate_posix_console_presentation() -> None:
    source = read("src/sys/posix/posix_syscon.cpp")

    require(source, '#include "../sys_console_theme.h"', "POSIX console shared theme include")

    # Every colour must come from the shared palette. Bare SDL colour literals
    # are how the two consoles drifted apart in the first place.
    for literal in ("0x1b, 0x20, 0x0a", "0xf0, 0x9e, 0x0d", "0x3a, 0x3f, 0x27", "0x79, 0x82, 0x50"):
        reject(source, literal, "POSIX console hard-coded palette literal")

    require(source, "SYSCON_METRIC_WINDOW_W", "POSIX console shared window width")
    require(source, "SYSCON_METRIC_MIN_W", "POSIX console shared minimum width")
    require(source, "SYSCON_METRIC_LINE_H", "POSIX console shared log line pitch")
    require(source, "SYSCON_METRIC_SCROLLBAR_W", "POSIX console shared scroll gutter")

    layout = function_body(source, "static void Posix_ConsoleUpdateLayout( void ) {")
    require(layout, "POSIX_CONSOLE_MIN_WIDTH", "POSIX console layout minimum clamp")
    require(layout, "POSIX_CONSOLE_MIN_HEIGHT", "POSIX console layout minimum clamp")
    require(layout, "scrollTrackRect", "POSIX console scroll gutter rect")
    require(layout, "buttonRects[ POSIX_CONSOLE_BUTTON_COPY ]", "POSIX console button rects")
    require(layout, "buttonRects[ POSIX_CONSOLE_BUTTON_QUIT ]", "POSIX console button rects")

    create = function_body(source, "static bool Posix_ConsoleCreateWindow( void ) {")
    require(
        create,
        "SDL_SetWindowMinimumSize( s_consoleWindow.window, POSIX_CONSOLE_MIN_WIDTH, POSIX_CONSOLE_MIN_HEIGHT )",
        "POSIX console enforced minimum window size",
    )

    render = function_body(source, "static void Posix_ConsoleRender( void ) {")
    # The defect this pins: advancing the log by the glyph cell instead of the
    # shared line pitch makes descenders land on the next row's ascenders.
    require(render, "y += static_cast<float>( POSIX_CONSOLE_LINE_HEIGHT );", "POSIX console log leading")
    reject(render, "y += POSIX_CONSOLE_FONT_SIZE;", "POSIX console log without leading")
    require(render, "POSIX_CONSOLE_LINE_HEIGHT", "POSIX console visible-line count uses the shared pitch")
    require(render, "Posix_ConsoleDrawScrollbar(", "POSIX console scroll indicator")
    require(render, "SYSCON_RGB( WINDOW )", "POSIX console themed window background")
    require(render, "SYSCON_RGB( INPUT )", "POSIX console themed command line")
    require(render, "s_consoleWindow.inputFocused", "POSIX console command-line focus ring")
    require(render, "SDL_GetTicks()", "POSIX console blinking caret")

    scrollbar = function_body(
        source, "static void Posix_ConsoleDrawScrollbar( int totalLines, int visibleLines, int firstLine ) {"
    )
    require(scrollbar, "if ( totalLines <= visibleLines || visibleLines <= 0 ) {", "POSIX console scroll indicator overflow guard")
    require(scrollbar, "SYSCON_RGB( SCROLL_TRACK )", "POSIX console scroll track")
    require(scrollbar, "SYSCON_RGB( SCROLL_THUMB )", "POSIX console scroll thumb")

    draw_button = function_body(source, "static void Posix_ConsoleDrawButton( int button ) {")
    require(draw_button, 'const char *buttonLabel = label != NULL ? label : "";', "POSIX console button label guard")
    require(draw_button, "SYSCON_RGB( BUTTON_DOWN )", "POSIX console pressed button state")
    require(draw_button, "SYSCON_RGB( BUTTON_HOT )", "POSIX console hover button state")

    status = function_body(source, "static void Posix_ConsoleDrawStatus( const char *fatalText ) {")
    require(status, "SYSCON_RGB( ALERT_PANEL )", "POSIX console fatal status background")
    require(status, "SYSCON_RGB( ALERT )", "POSIX console fatal status text")
    require(status, "statusText = SYSCON_STATUS_READY_TEXT;", "POSIX console ready status")

    # Activating on release rather than press; a Quit that fires on mouse-down
    # cannot be cancelled by dragging off it, and the Win32 console's native
    # buttons have always activated on release.
    click = function_body(source, "static void Posix_ConsoleClickButton( float x, float y ) {")
    require(click, "Posix_ConsoleWindowToRenderCoordinates( x, y );", "POSIX console HiDPI button input")
    require(click, "pressed != s_consoleWindow.hotButton", "POSIX console release-over-press activation")

    process_event = function_body(source, "bool Posix_ConsoleProcessEvent( const void *eventData ) {")
    require(process_event, "Posix_ConsoleTrackPointer( event.motion.x, event.motion.y );", "POSIX console hover tracking")
    require(process_event, "Posix_ConsoleBeginPress( event.button.x, event.button.y );", "POSIX console press tracking")
    require(process_event, "Posix_ConsoleClickButton( event.button.x, event.button.y );", "POSIX console release activation")
    require(process_event, "SDL_EVENT_WINDOW_MOUSE_LEAVE", "POSIX console hover clear on pointer leave")


def validate_windows_console_presentation() -> None:
    source = read("src/sys/win32/win_syscon.cpp")

    require(source, '#include "../sys_console_theme.h"', "Windows console shared theme include")
    require(source, "#define SYSCON_WIN_RGB( name )\tSysCon_Color( SYSCON_RGB( name ) )", "Windows console shared palette accessor")

    for literal in ("RGB(0x1b, 0x20, 0x0a)", "RGB(0xf0, 0x9e, 0x0d)"):
        reject(source, literal, "Windows console hard-coded palette literal")

    # Fixed pixel geometry against a per-monitor DPI aware process gives a
    # physically tiny window with clipped text on any scaled display.
    for literal in ("6, 5, 526, 30", "6, 400, 528, 20", "5, 425, 72, 24", "6, 40, 526, 354"):
        reject(source, literal, "Windows console hard-coded child geometry")

    require(source, "static int SysCon_Scale(int designUnits) {", "Windows console DPI metric scaling")
    require(source, "static int SysCon_QueryDpi(HWND hWnd) {", "Windows console per-monitor DPI query")
    require(source, '"GetDpiForWindow"', "Windows console per-monitor DPI query")

    layout = function_body(source, "static void SysCon_LayoutChildren(void) {")
    for metric in (
        "SYSCON_METRIC_MARGIN",
        "SYSCON_METRIC_GUTTER",
        "SYSCON_METRIC_STATUS_H",
        "SYSCON_METRIC_INPUT_H",
        "SYSCON_METRIC_BUTTON_W",
        "SYSCON_METRIC_BUTTON_H",
    ):
        require(layout, metric, "Windows console shared layout metrics")
    require(layout, "SysCon_Scale(", "Windows console DPI-scaled layout")

    create = function_body(source, "void Sys_CreateConsole(void) {")
    require(create, "int DEDSTYLE = WS_OVERLAPPEDWINDOW;", "Windows console resizable window")
    require(create, "SYSCON_METRIC_WINDOW_W", "Windows console shared default size")
    require(create, "SysCon_ApplyDarkWindowFrame(s_wcd.hWnd);", "Windows console dark title bar")
    require(create, "SysCon_ApplyDarkScrollbars(s_wcd.hwndBuffer);", "Windows console dark log scrollbar")
    require(create, "SysCon_CreateFonts();", "Windows console font creation")
    require(create, "SysCon_ApplyFonts();", "Windows console font assignment")
    require(create, "SysCon_LayoutChildren();", "Windows console initial layout")
    require(create, "BS_OWNERDRAW", "Windows console themed buttons")
    require(create, "SYSCON_LABEL_COPY", "Windows console shared button labels")
    require(create, "SYSCON_LABEL_QUIT", "Windows console shared button labels")
    # BS_PUSHBUTTON here would restore the light system button face.
    reject(create, "BS_PUSHBUTTON", "Windows console unthemed system button face")

    fonts = function_body(source, "static void SysCon_CreateFonts(void) {")
    require(fonts, '"Consolas"', "Windows console monospace log face")
    require(fonts, '"Segoe UI"', "Windows console UI chrome face")
    require(fonts, "SYSCON_METRIC_MONO_PT", "Windows console shared monospace size")
    require(fonts, "SYSCON_METRIC_UI_PT", "Windows console shared UI size")
    # hfButtonFont existed but was never created, so the chrome fell back to the
    # ancient system bitmap font.
    require(fonts, "s_wcd.hfButtonFont = CreateFont(", "Windows console UI font is actually created")

    wndproc = function_body(source, "static LRESULT CALLBACK ConWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {")
    # A writable edit sends WM_CTLCOLOREDIT, not WM_CTLCOLORSTATIC; without this
    # the command line keeps the default white system field.
    require(wndproc, "case WM_CTLCOLOREDIT:", "Windows console themed command line")
    require(wndproc, "s_wcd.hbrInputBackground", "Windows console themed command line brush")
    require(wndproc, "case WM_DRAWITEM:", "Windows console owner-drawn buttons")
    require(wndproc, "case WM_ERASEBKGND:", "Windows console themed background")
    require(wndproc, "SysCon_DrawPanelFrames((HDC)wParam);", "Windows console themed panel borders")
    require(wndproc, "SysCon_InvalidateInputFrame();", "Windows console focus-ring repaint")

    # WS_BORDER/SS_SUNKEN draw system-coloured 3D edges that read as light grey
    # scratches on a dark console, so the panel outlines are painted by hand.
    frames = function_body(source, "static void SysCon_DrawPanelFrames(HDC hdc) {")
    require(frames, "SYSCON_WIN_RGB(BORDER_LIT)", "Windows console panel border colour")
    require(frames, "SYSCON_WIN_RGB(ALERT)", "Windows console fatal panel border colour")
    require(frames, "GetFocus() == s_wcd.hwndInputLine", "Windows console command-line focus ring")
    require(wndproc, "case WM_SIZE:", "Windows console resize reflow")
    require(wndproc, "case WM_GETMINMAXINFO:", "Windows console minimum size")
    require(wndproc, "case WM_DPICHANGED:", "Windows console DPI change handling")
    require(wndproc, "SYSCON_METRIC_MIN_W", "Windows console shared minimum size")
    require(wndproc, "s_wcd.errorIsFatal", "Windows console fatal status treatment")
    require(wndproc, "SYSCON_WIN_RGB(ALERT_PANEL)", "Windows console fatal status background")

    draw_button = function_body(source, "static void SysCon_DrawButton(const DRAWITEMSTRUCT* item) {")
    require(draw_button, "SYSCON_WIN_RGB(BUTTON_DOWN)", "Windows console pressed button state")
    require(draw_button, "SYSCON_WIN_RGB(BUTTON_HOT)", "Windows console hover button state")
    require(draw_button, "s_wcd.hotButton", "Windows console hover state")

    button_proc = function_body(source, "static LRESULT CALLBACK SysConButtonWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {")
    require(button_proc, "TrackMouseEvent(&track);", "Windows console hover tracking")
    require(button_proc, "case WM_MOUSELEAVE:", "Windows console hover clear")

    # GDI objects created once per console must not leak when it is torn down
    # and rebuilt (vid_restart, dedicated-server toggles).
    destroy = function_body(source, "void Sys_DestroyConsole(void) {")
    require(destroy, "SysCon_DestroyFonts();", "Windows console font cleanup")
    for brush in (
        "&s_wcd.hbrWindowBackground",
        "&s_wcd.hbrEditBackground",
        "&s_wcd.hbrErrorBackground",
        "&s_wcd.hbrAlertBackground",
        "&s_wcd.hbrInputBackground",
    ):
        require(destroy, brush, "Windows console brush cleanup")


def validate_cross_platform_parity() -> None:
    """The two consoles must agree on the elements a user sees."""
    posix_source = read("src/sys/posix/posix_syscon.cpp")
    win_source = read("src/sys/win32/win_syscon.cpp")

    for token in (
        "SYSCON_METRIC_WINDOW_W",
        "SYSCON_METRIC_WINDOW_H",
        "SYSCON_METRIC_MIN_W",
        "SYSCON_METRIC_MIN_H",
        "SYSCON_METRIC_MARGIN",
        "SYSCON_METRIC_GUTTER",
        "SYSCON_METRIC_STATUS_H",
        "SYSCON_METRIC_INPUT_H",
        "SYSCON_METRIC_BUTTON_W",
        "SYSCON_METRIC_BUTTON_H",
        "SYSCON_METRIC_TEXT_PAD",
        "SYSCON_LABEL_COPY",
        "SYSCON_LABEL_CLEAR",
        "SYSCON_LABEL_QUIT",
        "SYSCON_STATUS_READY_TEXT",
    ):
        require(posix_source, token, "POSIX console shared presentation contract")
        require(win_source, token, "Windows console shared presentation contract")

    for entry in ("PANEL", "INPUT", "BUTTON", "BUTTON_HOT", "BUTTON_DOWN", "BORDER_LIT", "TEXT", "ALERT"):
        require(posix_source, f"SYSCON_RGB( {entry} )", "POSIX console shared palette use")
        require(win_source, f"SYSCON_WIN_RGB({entry})", "Windows console shared palette use")


def validate_posix_support_gl_binding() -> None:
    """The splash and console renderers must hand back the game's GL binding.

    Outside WSL, SDL backs a software renderer's window with a hidden OpenGL
    renderer. Destroying the splash after the first loading frame used to leave
    no GL context current, so the next frame's calls were dropped and its GPU
    timer ended with no matching begin (GL_INVALID_OPERATION on Linux CI).
    """
    source = read("src/sys/posix/posix_syscon.cpp")
    scope = function_body(source, "class Posix_SupportGLBindingScope {")
    for needle in ("SDL_GL_GetCurrentWindow()", "SDL_GL_GetCurrentContext()",
                   "SDL_GL_MakeCurrent( window, context )"):
        require(scope, needle, "support-window GL binding scope")
    for signature in ("static SDL_Renderer *Posix_CreateSupportRenderer(",
                      "static void Posix_SplashRender(",
                      "static void Posix_SplashDestroy(",
                      "static void Posix_ConsoleRender(",
                      "void Posix_ShutdownConsole("):
        require(function_body(source, signature), "const Posix_SupportGLBindingScope glBinding;",
                f"{signature} GL binding guard")


def main() -> int:
    validate_theme_header()
    validate_layout_fits()
    validate_posix_console_presentation()
    validate_windows_console_presentation()
    validate_cross_platform_parity()
    validate_posix_support_gl_binding()
    print("System console presentation checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
