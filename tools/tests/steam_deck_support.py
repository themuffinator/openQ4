#!/usr/bin/env python3
"""Regression checks for Steam Deck support contracts."""

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


def python_function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    next_function = source.find("\ndef ", start + len(signature))
    if next_function == -1:
        return source[start:]
    return source[start:next_function]


def require_order(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index == -1 or second_index == -1:
        raise AssertionError(f"Missing ordered symbols {first!r} and/or {second!r} in {context}")
    if first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def validate_launcher_templates() -> None:
    relative_path = "assets/linux/openQ4-steamdeck.in"
    launcher = read(relative_path)
    require(launcher, 'export OPENQ4_STEAMDECK="${OPENQ4_STEAMDECK:-1}"', relative_path)
    require(launcher, 'if [ "${OPENQ4_FORCE_X11:-0}" = "1" ]; then', relative_path)
    require(launcher, "export SDL_VIDEO_DRIVER=x11", relative_path)
    require(launcher, "export SDL_VIDEODRIVER=x11", relative_path)
    require(launcher, 'exec "$SCRIPT_DIR/@OPENQ4_CLIENT_BINARY@" +set com_platformProfile steamdeck "$@"', relative_path)
    reject(launcher, "WAYLAND_DISPLAY", f"{relative_path} should not force X11 in mixed Wayland sessions")


def validate_profile_defaults() -> None:
    profile = read("content/baseoq4/pak0/openq4_profile_steamdeck.cfg")
    for token in (
        'seta in_joystick "1"',
        'seta in_gyro "1"',
        'seta in_gyroSensitivity "0.20"',
        'seta in_gyroDeadZone "0.015"',
        'seta in_touchpadMode "1"',
        'seta in_touchpadSensitivity "1.0"',
        'seta in_touchscreen "1"',
        'seta com_steamDeckAutoFrameCap "1"',
        'seta com_steamDeckFrameCap "0"',
        'seta in_joystickLowBatteryRumbleThreshold "20"',
        'seta in_joystickLowBatteryRumbleScale "0.75"',
        # Handheld starts the HUD at 125% (ui-visual-design.md 13.9).
        'seta ui_hudScale "1.25"',
    ):
        require(profile, token, "Steam Deck platform profile")


def validate_common_profile_detection() -> None:
    source = read("src/framework/Common.cpp")
    host_signal = function_body(source, "static bool Common_HasSteamDeckHostSignal( void ) {")
    automatic = function_body(source, "void idCommonLocal::ApplyAutomaticPlatformProfile( void ) {")
    init = function_body(source, "void idCommonLocal::Init( int argc, const char **argv, const char *cmdline ) {")
    profile_name = function_body(source, "static idStr Common_BuildPlatformProfileConfigName( const char *profileName ) {")

    require(source, 'idCVar com_platformProfile( "com_platformProfile", "default"', "platform profile cvar")
    for token in (
        '"OPENQ4_STEAMDECK"',
        '"OPENQ4_AUTODETECT_STEAMDECK"',
        '"SteamDeck"',
        '"STEAM_DECK"',
        '"STEAMDECK"',
        '"steamdeck"',
        '"SteamOS"',
        '"STEAMOS"',
        '"steamos"',
        '"/sys/devices/virtual/dmi/id/product_name"',
        '"/sys/devices/virtual/dmi/id/board_name"',
        '"/etc/os-release"',
        '"/run/host/os-release"',
        '"VARIANT_ID=steamdeck"',
    ):
        require(host_signal, token, "Steam Deck host signal detection")

    require(automatic, 'com_platformProfile.GetString(), "default"', "automatic platform profile default gate")
    require(automatic, '"OPENQ4_DISABLE_STEAMDECK_AUTODETECT"', "automatic platform profile disable knob")
    require(automatic, '"OPENQ4_NO_STEAMDECK_AUTODETECT"', "automatic platform profile disable knob")
    require(automatic, "Common_HasSteamDeckHostSignal()", "automatic platform profile host signal")
    require(automatic, 'com_platformProfile.SetString( "steamdeck" );', "automatic platform profile assignment")
    require(profile_name, 'sanitized.Icmp( "steamdeck" )', "platform profile allow-list")
    require(profile_name, '"openq4_profile_%s.cfg"', "platform profile config mapping")
    require(init, "ApplyAutomaticPlatformProfile();", "Common init profile auto-detection")
    require_order(init, "StartupVariable( NULL, false );", "ApplyAutomaticPlatformProfile();", "Common init profile auto-detection")


def validate_filesystem_discovery() -> None:
    source = read("src/framework/FileSystem.cpp")
    candidates = function_body(source, "static void FS_BuildSteamInstallCandidates( idStrList &candidates ) {")

    for token in (
        '"OPENQ4_QUAKE4_PATH"',
        '"OPENQ4_QUAKE4_ROOT"',
        '"OPENQ4_STEAM_ROOT"',
        '"OPENQ4_STEAM_ROOTS"',
        '"STEAM_COMPAT_CLIENT_INSTALL_PATH"',
        '"OPENQ4_STEAM_LIBRARY"',
        '"OPENQ4_STEAM_LIBRARIES"',
        '"XDG_DATA_HOME"',
        '".steam"',
        '"root"',
        '".local"',
        '"com.valvesoftware.Steam"',
        '"steamapps"',
        '"common"',
        '"Quake 4"',
    ):
        require(candidates, token, "Steam install discovery candidates")

    require(source, '"libraryfolders.vdf"', "Steam libraryfolders parser")
    require(source, 'FS_LogPathList( "Steam install discovery roots", steamRoots );', "Steam discovery logging")
    require(source, 'FS_LogPathList( "Steam explicit library roots", explicitLibraryRoots );', "Steam discovery logging")
    require(source, 'FS_LogPathList( "Steam library roots to probe", discoveryLibraryRoots );', "Steam discovery logging")
    require(source, 'FS_LogPathList( "Steam Quake 4 install candidates", candidates );', "Steam discovery logging")


def validate_sdl3_input_and_lifecycle() -> None:
    source = read("src/sys/sdl3/sdl3_backend.cpp")
    gyro = function_body(source, "static void SDL3_HandleGamepadGyroEvent(const SDL_GamepadSensorEvent &event, int eventTime) {")
    touchpad = function_body(source, "static void SDL3_HandleGamepadTouchpadEvent(const SDL_GamepadTouchpadEvent &event, int eventTime) {")
    finger = function_body(source, "static void SDL3_HandleFingerEvent(const SDL_TouchFingerEvent &event, int eventTime) {")
    pump = function_body(source, "bool Sys_SDL_PumpEvents(void) {")
    background = function_body(source, "static void SDL3_HandleAppBackgroundTransition(int eventTime, const char *reason) {")
    foreground = function_body(source, "static void SDL3_HandleAppForegroundTransition(int eventTime, const char *reason) {")
    event_watch = function_body(source, "static bool SDLCALL SDL3_LifecycleEventWatch(void *userdata, SDL_Event *event) {")
    register_watch = function_body(source, "static void SDL3_RegisterLifecycleEventWatch(void) {")
    unregister_watch = function_body(source, "static void SDL3_UnregisterLifecycleEventWatch(void) {")
    process_pending = function_body(source, "static void SDL3_ProcessPendingLifecycleEvents(int eventTime) {")
    perf = function_body(source, "static void SDL3_ApplySteamDeckPerformanceDefaults(void) {")
    diagnostics = function_body(source, "static void SDL3_ListControllers_f(const idCmdArgs &args) {")
    gamepad_details = function_body(source, "static void SDL3_PrintActiveGamepadDetails(void) {")
    # GLimp_Init moved into the renderer-gl module and now delegates window and
    # input startup to this window-services entry point, which the module calls
    # before it creates any GL context.
    startup = function_body(source, "static bool SDL3_WindowServices_PrepareWindowSystem(void) {")
    require(
        read("src/renderer/OpenGL/gl_ContextSDL3.cpp"),
        "s_glWindowServices->PrepareWindowSystem()",
        "renderer-gl module window/input startup handoff",
    )

    for token in (
        'idCVar in_joystickLowBatteryRumbleThreshold("in_joystickLowBatteryRumbleThreshold"',
        'idCVar in_joystickLowBatteryRumbleScale("in_joystickLowBatteryRumbleScale"',
        'idCVar in_gyro("in_gyro"',
        'idCVar in_gyroSensitivity("in_gyroSensitivity"',
        'idCVar in_gyroDeadZone("in_gyroDeadZone"',
        'idCVar in_touchpadMode("in_touchpadMode"',
        'idCVar in_touchpadSensitivity("in_touchpadSensitivity"',
        'idCVar in_touchscreen("in_touchscreen"',
        'idCVar com_steamDeckAutoFrameCap("com_steamDeckAutoFrameCap"',
        'idCVar com_steamDeckFrameCap("com_steamDeckFrameCap"',
    ):
        require(source, token, "SDL3 Steam Deck cvars")

    require(source, "SDL_GetGamepadPowerInfo", "low-battery rumble power query")
    require(source, "SDL3_GetEffectiveRumbleScale", "low-battery rumble scale path")
    require(source, "SDL_GamepadHasSensor(s_sdlGamepad, SDL_SENSOR_GYRO)", "gyro capability detection")
    require(source, "SDL_SetGamepadSensorEnabled(s_sdlGamepad, SDL_SENSOR_GYRO, wantsGyro)", "gyro sensor enable")
    require(gyro, "SDL_SENSOR_GYRO", "gyro event filtering")
    require(gyro, "SDL3_IsMouseCaptured()", "gyro gameplay routing")
    require(gyro, "SDL3_ShouldRouteMenuMouse()", "gyro menu suppression")
    require(gyro, "SDL3_QueueMouseDelta(dx, dy, eventTime);", "gyro mouse delta queueing")

    require(touchpad, "in_touchpadMode.GetInteger()", "touchpad mode gate")
    require(touchpad, "mode == 1", "touchpad menu cursor mode")
    require(touchpad, "mode == 2", "touchpad mouse-look mode")
    require(touchpad, "SDL3_ShouldRouteMenuMouse()", "touchpad menu routing")
    require(touchpad, "SDL3_IsMouseCaptured()", "touchpad gameplay routing")
    require(touchpad, "SDL3_QueueMouseDelta(dx, dy, eventTime);", "touchpad mouse delta queueing")

    require(finger, "in_touchscreen.GetBool()", "touchscreen route cvar")
    require(finger, "SDL_EVENT_FINGER_CANCELED", "touchscreen cancel handling")
    require(finger, "SDL3_SetRoutedCursorFromWindowPosition", "touchscreen cursor routing")
    require(finger, "openQ4_AcceptingLoadingContinueInput()", "touchscreen loading-continue routing")
    require(finger, "SDL3_QueueMouseButtonEvent(K_MOUSE1", "touchscreen tap queueing")

    for token in (
        "SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN",
        "SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION",
        "SDL_EVENT_GAMEPAD_TOUCHPAD_UP",
        "SDL_EVENT_GAMEPAD_SENSOR_UPDATE",
        "SDL_EVENT_FINGER_DOWN",
        "SDL_EVENT_FINGER_MOTION",
        "SDL_EVENT_FINGER_UP",
        "SDL_EVENT_FINGER_CANCELED",
        "SDL_EVENT_WILL_ENTER_BACKGROUND",
        "SDL_EVENT_DID_ENTER_BACKGROUND",
        "SDL_EVENT_WILL_ENTER_FOREGROUND",
        "SDL_EVENT_DID_ENTER_FOREGROUND",
    ):
        require(pump, token, "SDL3 event pump Steam Deck routing")

    for token in (
        "SDL_EVENT_TERMINATING",
        "SDL_EVENT_LOW_MEMORY",
        "SDL_EVENT_WILL_ENTER_BACKGROUND",
        "SDL_EVENT_DID_ENTER_BACKGROUND",
        "SDL_EVENT_WILL_ENTER_FOREGROUND",
        "SDL_EVENT_DID_ENTER_FOREGROUND",
        "SDL3_SetLifecyclePendingFlag",
    ):
        require(event_watch, token, "SDL3 lifecycle event watch")
    require(register_watch, "SDL_AddEventWatch(SDL3_LifecycleEventWatch, NULL)", "SDL3 lifecycle event watch registration")
    require(unregister_watch, "SDL_RemoveEventWatch(SDL3_LifecycleEventWatch, NULL)", "SDL3 lifecycle event watch removal")
    require(startup, "SDL3_RegisterLifecycleEventWatch();", "SDL3 lifecycle event watch startup")
    require(source, "SDL3_UnregisterLifecycleEventWatch();", "SDL3 lifecycle event watch shutdown")
    require(pump, "SDL3_ProcessPendingLifecycleEvents(Sys_Milliseconds());", "SDL3 pending lifecycle processing")
    require(process_pending, "SDL3_HandleAppBackgroundTransition(eventTime, \"event watch\")", "SDL3 pending background processing")
    require(process_pending, "SDL3_HandleAppForegroundTransition(eventTime, \"event watch\")", "SDL3 pending foreground processing")

    for token in (
        "SDL3_ReleaseFocusInputState(eventTime);",
        "common->WriteConfigToFile(CONFIG_FILE);",
    ):
        require(background, token, "SDL3 background transition")
    for token in (
        "SDL3_InvalidateMenuMouseRouting();",
        "SDL3_ClearInputQueues();",
        "SDL3_OpenFirstController();",
        "SDL3_UpdateGamepadSensorState(false);",
        "Sys_GrabMouseCursor(true);",
        "SDL3_UpdateCursorVisibility();",
    ):
        require(foreground, token, "SDL3 foreground transition")

    require(perf, "SDL3_IsSteamDeckPlatformProfile()", "Steam Deck performance defaults")
    require(perf, "OPENQ4_GLOBAL_DEFAULT_MAXFPS = 240", "Steam Deck performance defaults")
    require(perf, "com_maxfps.SetInteger(deckFrameCap);", "Steam Deck performance defaults")
    require(startup, "SDL3_ApplySteamDeckPerformanceDefaults();", "SDL3 startup performance defaults")
    require(startup, 'cmdSystem->AddCommand("listControllers"', "SDL3 controller diagnostics command")

    for token in (
        "SDL3 controller diagnostics",
        "Steam Deck profile:",
        "touchscreen route:",
        "lowBatteryThreshold",
        "SDL3_PrintActiveGamepadDetails();",
        "SDL3_PrintActiveJoystickDetails();",
    ):
        require(diagnostics, token, "listControllers diagnostics output")

    for token in (
        "touchpads:",
        "gyro route:",
        "touchpad route:",
    ):
        require(gamepad_details, token, "active gamepad diagnostics output")


def validate_menu_and_docs() -> None:
    game_gui = read("content/baseoq4/pak0/guis/menu/settings/game.gui")
    registry = read("docs/dev/settings-menu-registry.json")
    release = read("docs/dev/release-completion.md")
    steam_deck = read("docs/user/steam-deck.md")
    input_settings = read("docs/user/input-settings.md")
    getting_started = read("docs/user/getting-started.md")
    package_readme = read("assets/release/README.html")
    qa = read("docs/dev/steam-deck-qa.md")

    for token in (
        "set_game_controller_gyro",
        "set_game_controller_gyro_sensitivity",
        "set_game_controller_touchpad_mode",
        "set_game_controller_touchpad_sensitivity",
        "set_game_controller_touchscreen",
        "set_game_controller_low_battery_rumble",
        "set_game_controller_low_battery_rumble_cap",
    ):
        require(game_gui, token, "Game Options Controller Deck rows")
        require(registry, token, "settings registry Deck rows")

    for token in (
        "OPENQ4_FORCE_X11=1",
        "listControllers",
        "Touch API pass-through",
        "in_gyro",
        "in_touchpadMode",
        "in_touchscreen",
        "in_joystickLowBatteryRumbleThreshold",
    ):
        require(steam_deck + input_settings + qa, token, "Steam Deck user and QA docs")

    require(release, "Steam Deck support is more complete", "release completion notes")
    require(release, "Steam Deck setup is easier to diagnose", "release completion notes")
    require(release, "Steam Deck controls are easier to tune in-game", "release completion notes")

    package_docs = getting_started + package_readme
    for token in (
        "openQ4-steamdeck",
        "OPENQ4_STEAMDECK=1",
        "Direct",
        "auto-select",
        "OPENQ4_FORCE_X11=1",
        "Settings → Game Options → Controller",
        "Settings -&gt; Game Options -&gt; Controller",
        "listControllers",
    ):
        require(package_docs, token, "Steam Deck package-facing docs")


def validate_packaging_metadata_checks() -> None:
    runner = read("tools/validation/openq4_validate.py")
    packager = read("tools/build/package_nightly.py")
    staged = python_function_body(runner, "def validate_linux_steamdeck_launcher(path: Path, root: Path, client_candidates: list[Path]) -> None:")
    packaged = python_function_body(packager, "def validate_linux_steamdeck_launcher(path: Path, expected_client: str) -> None:")
    staged_launch_metadata = python_function_body(runner, "def validate_linux_launch_metadata(root: Path, install_root: Path, client_candidates: list[Path]) -> None:")
    package_metadata = python_function_body(packager, "def validate_linux_package_metadata(package_root: Path, arch: str, *, allow_missing_binaries: bool = False) -> None:")

    for body, context in (
        (staged, "staged Steam Deck launcher validation"),
        (packaged, "packaged Steam Deck launcher validation"),
    ):
        for token in (
            "OPENQ4_STEAMDECK",
            "OPENQ4_FORCE_X11",
            "SDL_VIDEO_DRIVER=x11",
            "SDL_VIDEODRIVER=x11",
            "+set com_platformProfile steamdeck",
            "WAYLAND_DISPLAY",
        ):
            require(body, token, context)

    require(staged, "client_candidates", "staged Steam Deck launcher client target validation")
    require(packaged, "expected_client", "packaged Steam Deck launcher client target validation")
    require(staged_launch_metadata, "validate_linux_steamdeck_launcher(steamdeck_launcher, root, client_candidates)", "staged Linux launch metadata validation")
    require(package_metadata, "validate_linux_steamdeck_launcher(steamdeck_launcher, client_binary)", "packaged Linux launch metadata validation")


def validate_couch_profile() -> None:
    """The Couch viewing profile (ui-visual-design.md 13.2) shares the platform
    profile machinery: Big Picture (SteamTenfoot) picks it on anything but a
    Steam Deck, and its config raises the interface, text and HUD scales."""
    profile = read("content/baseoq4/pak0/openq4_profile_couch.cfg")
    for token in (
        'seta in_joystick "1"',
        'seta ui_retainedScale "1.5"',
        'seta ui_retainedTextScale "1.25"',
        'seta ui_hudScale "1.25"',
    ):
        require(profile, token, "Couch platform profile")
    source = read("src/framework/Common.cpp")
    host_signal = function_body(source, "static bool Common_HasCouchHostSignal( void ) {")
    automatic = function_body(source, "void idCommonLocal::ApplyAutomaticPlatformProfile( void ) {")
    profile_name = function_body(source, "static idStr Common_BuildPlatformProfileConfigName( const char *profileName ) {")
    for token in ('"OPENQ4_COUCH"', '"OPENQ4_AUTODETECT_COUCH"', '"SteamTenfoot"'):
        require(host_signal, token, "Couch host signal detection")
    require(automatic, 'com_platformProfile.SetString( "couch" );', "automatic Couch profile assignment")
    require_order(automatic, 'com_platformProfile.SetString( "steamdeck" );', "Common_HasCouchHostSignal()",
                  "a Steam Deck keeps the Handheld profile in Big Picture")
    require(profile_name, 'sanitized.Icmp( "couch" )', "platform profile allow-list")
    require(read("tools/build/check_staged_content_edits.py"), '"openq4_profile_couch.cfg"', "stale loose profile pruning")
    require(read("docs/user/display-settings.md"), "com_platformProfile couch", "Couch profile player docs")


def validate_ci_smoke() -> None:
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")
    runner = read("tools/validation/openq4_validate.py")

    require(push, "tools/tests/steam_deck_support.py", "push workflow Python compile list")
    require(push, "python tools/tests/steam_deck_support.py", "push script-smoke regression check")
    require(commit, "tools/tests/steam_deck_support.py", "commit workflow Python compile list")
    require(commit, "python tools/tests/steam_deck_support.py", "commit script-smoke regression check")
    require(runner, "steam_deck_support.py", "validation Python tests")


def main() -> None:
    validate_launcher_templates()
    validate_profile_defaults()
    validate_common_profile_detection()
    validate_couch_profile()
    validate_filesystem_discovery()
    validate_sdl3_input_and_lifecycle()
    validate_menu_and_docs()
    validate_packaging_metadata_checks()
    validate_ci_smoke()
    print("steam_deck_support: ok")


if __name__ == "__main__":
    main()
