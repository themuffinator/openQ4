#!/usr/bin/env python3
"""Regression checks for the SP loading-screen continue gate input on Linux/macOS."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    data = (ROOT / relative_path).read_bytes()
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return data.decode("cp1252")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


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


def require_order(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index == -1 or second_index == -1:
        raise AssertionError(f"Missing ordered symbols {first!r} and/or {second!r} in {context}")
    if first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def validate_posix_event_pump() -> None:
    """The continue gate spins on Sys_GenerateEvents; the POSIX implementation must
    actually pump SDL window events or no key/mouse input can ever dismiss the gate."""
    source = read("src/sys/posix/posix_main.cpp")
    generate = function_body(source, "void Sys_GenerateEvents( void ) {")

    require(generate, "Sys_SDL_PumpEvents();", "POSIX Sys_GenerateEvents SDL pump")
    require(generate, "IN_Frame();", "POSIX Sys_GenerateEvents input frame")
    require(generate, "Posix_ConsoleInput()", "POSIX Sys_GenerateEvents console polling")


def validate_session_gate() -> None:
    source = read("src/framework/Session.cpp")

    gate_start = source.find("bool waitingForContinue = true;")
    if gate_start == -1:
        raise AssertionError("Missing loading-continue gate in Session.cpp")
    gate_end = source.find("// capture the current screen", gate_start)
    if gate_end == -1:
        raise AssertionError("Could not find end of loading-continue gate cleanup in Session.cpp")
    gate = source[gate_start:gate_end]

    require(gate, "Sys_GenerateEvents();", "loading-continue gate event pump")
    require(gate, "eventLoop->GetEvent()", "loading-continue gate event drain")
    require(gate, "Session_IsLoadingContinueKey( ev.evValue )", "loading-continue gate key acceptance")
    require(gate, "Session_IsLoadingContinueChar( ev.evValue )", "loading-continue gate char acceptance")
    require_order(
        gate,
        "idKeyInput::ClearStates();",
        "Sys_ClearInputEvents();",
        "loading-continue gate clears gameplay input queues",
    )

    # The platform backends only queue mouse buttons while this flag is raised
    # (or while the mouse is captured / menu-routed), so the gate must bracket
    # its wait loop with it for clicks to work.
    require_order(
        gate,
        "openQ4_SetLoadingContinueInputActive( true );",
        "while ( waitingForContinue ) {",
        "loading-continue gate input-active begin",
    )
    require_order(
        gate,
        "while ( waitingForContinue ) {",
        "openQ4_SetLoadingContinueInputActive( false );",
        "loading-continue gate input-active end",
    )


def validate_sdl3_backend_input() -> None:
    source = read("src/sys/sdl3/sdl3_backend.cpp")
    pump = function_body(source, "bool Sys_SDL_PumpEvents(void) {")
    button_helper = function_body(source, "static void SDL3_QueueMouseButtonEvent(int key, bool down, int eventTime, bool pollState) {")

    # Unclaimed keyboard events still reach the legacy loading gate, carrying
    # the modifier/repeat metadata used by both UI implementations.
    require(pump, "if (!retained && key != K_PRINT_SCR", "SDL3 legacy keyboard fallback")
    require(pump, "Sys_QueEvent(eventTime, SE_KEY, key, down, static_cast<int>(metadata.size()), payload);", "SDL3 keyboard SE_KEY queueing")
    require(pump, "SDL_EVENT_TEXT_INPUT", "SDL3 text input handling")

    # Mouse buttons are normally dropped when the mouse is neither captured nor
    # routed to a menu; the loading-continue gate opts back in via the common flag.
    require(pump, "openQ4_AcceptingLoadingContinueInput()", "SDL3 loading-continue mouse acceptance")
    button_case = pump[pump.find("case SDL_EVENT_MOUSE_BUTTON_DOWN:") :]
    button_case = button_case[: button_case.find("case SDL_EVENT_MOUSE_WHEEL:")]
    require(button_case, "openQ4_AcceptingLoadingContinueInput()", "SDL3 mouse button gate acceptance")
    require(button_case, "SDL3_QueueMouseButtonEvent(key, down, eventTime, routedMouseInput);", "SDL3 mouse button helper queueing")
    require(button_helper, "Sys_QueEvent(eventTime, SE_KEY, key, down ? 1 : 0, 0, NULL);", "SDL3 mouse button SE_KEY queueing")
    require_order(
        button_helper,
        "if (pollState) {",
        "SDL3_QueueMouseInput(M_ACTION1",
        "SDL3 mouse poll queueing stays capture/menu-routed only",
    )


def validate_common_flag() -> None:
    header = read("src/framework/Common.h")
    source = read("src/framework/Common.cpp")

    require(header, "void				openQ4_SetLoadingContinueInputActive( bool active );", "Common.h gate flag setter")
    require(header, "bool				openQ4_AcceptingLoadingContinueInput( void );", "Common.h gate flag getter")
    require(source, "void openQ4_SetLoadingContinueInputActive( bool active ) {", "Common.cpp gate flag setter")
    require(source, "bool openQ4_AcceptingLoadingContinueInput( void ) {", "Common.cpp gate flag getter")


def validate_input_flush_hook() -> None:
    header = read("src/sys/sys_public.h")
    sdl3 = read("src/sys/sdl3/sdl3_backend.cpp")
    win32 = read("src/sys/win32/win_input.cpp")
    posix = read("src/sys/posix/posix_input.cpp")

    require(header, "void			Sys_ClearInputEvents( void );", "Sys_ClearInputEvents declaration")
    require(sdl3, "void Sys_ClearInputEvents(void) {", "SDL3 input flush hook")
    require(sdl3, "SDL3_ClearInputQueues();", "SDL3 input queue flush")
    require(win32, "void Sys_ClearInputEvents( void ) {", "Win32 input flush hook")
    require(win32, "IN_ClearBufferedDeviceData( win32.g_pKeyboard );", "Win32 keyboard buffer flush")
    require(win32, "IN_ClearBufferedDeviceData( win32.g_pMouse );", "Win32 mouse buffer flush")
    require(posix, "void Sys_ClearInputEvents( void ) {", "POSIX input flush hook")
    require(posix, "poll_keyboard_event_count = 0;", "POSIX keyboard poll flush")
    require(posix, "poll_mouse_event_count = 0;", "POSIX mouse poll flush")


def validate_platform_sources() -> None:
    """Linux and macOS SDL3 builds must link the POSIX Sys_GenerateEvents that pumps SDL."""
    source = read("tools/build/meson_sources.py")

    linux_block = source[source.find("SDL3_LINUX_SOURCES") : source.find("LINUX_X11_HELPER_SOURCES")]
    darwin_block = source[source.find("SDL3_DARWIN_SOURCES") : source.find("LINUX_PLATFORM_SOURCES")]

    require(linux_block, "sys/posix/posix_main.cpp", "Linux SDL3 platform sources")
    require(linux_block, "sys/linux/linux_sdl3.cpp", "Linux SDL3 platform sources")
    require(darwin_block, "sys/posix/posix_main.cpp", "macOS SDL3 platform sources")
    require(darwin_block, "sys/macos/macos_sdl3.cpp", "macOS SDL3 platform sources")


def validate_loading_screen_sounds() -> None:
    """The stock screens' load_screen_ready and load_screen_click are "play"
    menu commands that the gate never dispatches, and the retained screen has no
    scripts, so the session plays each one once per gate (menu-sounds.md)."""
    for gui in ("generic", "intro"):
        text = read(f"content/baseoq4/pak0/guis/loading/{gui}.gui")
        require_order(text, "onNamedEvent FinishedLoading", 'set "cmd" "play load_screen_ready"', f"stock {gui}.gui completion sound")
        require_order(text, "onAction", 'set "cmd" "play load_screen_click"', f"stock {gui}.gui press sound")

    source = read("src/framework/Session.cpp")
    for shader in ("load_screen_ready", "load_screen_click"):
        count = source.count(f'"{shader}"')
        if count != 1:
            raise AssertionError(f"Expected one session play of {shader}, found {count}")

    chooser = function_body(source, "static bool Session_LoadingScreenPlaysStockSounds(")
    require(chooser, "retained ||", "retained loading screen sounds")
    require(chooser, '"guis/loading/generic.gui"', "stock generic loading screen sounds")
    require(chooser, '"guis/loading/intro.gui"', "stock intro loading screen sounds")
    player = function_body(source, "static void Session_PlayLoadingScreenSound(")
    require(player, "soundSystem->GetPlayingSoundWorld()", "loading screen sounds play in the playing world")
    require(player, "PlayShaderDirectly( shader, 1 )", "loading screen sounds use the menu channel")

    change = function_body(source, "void idSessionLocal::ExecuteMapChange( bool noFadeWipe ) {")
    gate = change[change.find("if ( waitForSPContinue ) {") :]
    # The stinger plays in the unmuted menu world on a fresh sound clock, after
    # the prompt's event, and the held gate keeps mixing it.
    for first, second in (
        ('HandleNamedEvent( "FinishedLoading" )', "SetPlayingSoundWorld( menuSoundWorld );"),
        ("SetPlayingSoundWorld( menuSoundWorld );", "soundSystem->SetMute( false );"),
        ("soundSystem->SetMute( false );", "soundSystem->Render();"),
        ("soundSystem->Render();", 'Session_PlayLoadingScreenSound( "load_screen_ready"'),
        ('Session_PlayLoadingScreenSound( "load_screen_ready"', "bool waitingForContinue = true;"),
    ):
        require_order(gate, first, second, "loading screen completion sound")
    loop = gate[gate.find("bool waitingForContinue = true;") : gate.find("openQ4_SetLoadingContinueInputActive( false );")]
    require(loop, "soundSystem->Render();", "held loading gate mixes its sound")
    # The press plays once the game world has taken over and sound is back on.
    tail = change[change.find("SetPlayingSoundWorld( sw );") :]
    require_order(tail, "soundSystem->SetMute( false );", 'Session_PlayLoadingScreenSound( "load_screen_click"', "loading screen press sound")
    require_order(change, "loadingScreenContinued = loadingScreenSounds;", "SetPlayingSoundWorld( sw );", "loading screen press recorded at the gate")


def validate_ci_smoke() -> None:
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")
    runner = read("tools/validation/openq4_validate.py")

    require(push, "python tools/tests/loading_continue_input.py", "push script-smoke regression check")
    require(commit, "python tools/tests/loading_continue_input.py", "commit script-smoke regression check")
    require(runner, "loading_continue_input.py", "validation Python tests")


def main() -> None:
    validate_posix_event_pump()
    validate_session_gate()
    validate_sdl3_backend_input()
    validate_common_flag()
    validate_input_flush_hook()
    validate_platform_sources()
    validate_loading_screen_sounds()
    validate_ci_smoke()
    print("loading_continue_input: ok")


if __name__ == "__main__":
    main()
