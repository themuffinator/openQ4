#!/usr/bin/env python3
"""Regression checks for SDL3 input parity across Windows, Linux, and macOS."""

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


def require_order(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index == -1 or second_index == -1:
        raise AssertionError(f"Missing ordered symbols {first!r} and/or {second!r} in {context}")
    if first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def validate_shared_backend_sources() -> None:
    meson_sources = read("tools/build/meson_sources.py")

    linux_block = meson_sources[meson_sources.find("SDL3_LINUX_SOURCES") : meson_sources.find("LINUX_X11_HELPER_SOURCES")]
    darwin_block = meson_sources[meson_sources.find("SDL3_DARWIN_SOURCES") : meson_sources.find("LINUX_PLATFORM_SOURCES")]
    for token, context in (
        ('"src/sys/win32/win_sdl3.cpp"', "Windows SDL3 platform sources"),
        ('"sys/linux/linux_sdl3.cpp"', "Linux SDL3 platform sources"),
        ('"sys/macos/macos_sdl3.cpp"', "macOS SDL3 platform sources"),
    ):
        require(meson_sources, token, context)

    require(linux_block, "linux_sdl3.cpp", "Linux SDL3 wrapper source")
    require(linux_block, "sys/posix/posix_main.cpp", "Linux SDL3 POSIX event pump")
    require(darwin_block, "macos_sdl3.cpp", "macOS SDL3 wrapper source")
    require(darwin_block, "macos_sdl3_main.cpp", "macOS SDL_RunApp entry")
    require(darwin_block, "sys/posix/posix_main.cpp", "macOS SDL3 POSIX event pump")

    for relative_path, token in (
        ("src/sys/win32/win_sdl3.cpp", '#include "../sdl3/sdl3_backend.cpp"'),
        ("src/sys/linux/linux_sdl3.cpp", '#include "../sdl3/sdl3_backend.cpp"'),
        ("src/sys/macos/macos_sdl3.cpp", '#include "../sdl3/sdl3_backend.cpp"'),
    ):
        require(read(relative_path), token, relative_path)


def validate_keyboard_and_mouse_contract() -> None:
    source = read("src/sys/sdl3/sdl3_backend.cpp")
    mouse_hints = function_body(source, "static void SDL3_SetMouseHintDefaults(void) {")
    map_keycode = function_body(source, "static int SDL3_MapKeycode(SDL_Keycode keycode) {")
    map_scancode = function_body(source, "static int SDL3_MapScancode(SDL_Scancode scancode) {")
    control_char = function_body(source, "static int SDL3_MapControlChar(int key, bool down, SDL_Keymod modState) {")
    map_mouse_button = function_body(source, "static int SDL3_MapMouseButton(Uint8 button) {")
    queue_button = function_body(source, "static void SDL3_QueueMouseButtonEvent(int key, bool down, int eventTime, bool pollState) {")
    consume_delta = function_body(source, "static int SDL3_ConsumeMouseDelta(float delta, float &remainder) {")
    activate_mouse = function_body(source, "void IN_ActivateMouse(void) {")
    input_frame = function_body(source, "void IN_Frame(void) {")
    grab_mouse = function_body(source, "void Sys_GrabMouseCursor(bool grabIt) {")
    mouse_capture_diag = function_body(source, "static void SDL3_MouseCaptureDiagnostics_f(const idCmdArgs &args) {")
    init_input = function_body(source, "void Sys_InitInput(void) {")
    video_hints = function_body(source, "static void SDL3_SetVideoHintDefaults(void) {")
    text_input_area = function_body(source, "static void SDL3_UpdateTextInputArea(void) {")
    text_input_state = function_body(source, "static void SDL3_UpdateTextInputState(void) {")
    window_event = function_body(source, "static void SDL3_HandleWindowEvent(const SDL_WindowEvent &event, int eventTime) {")
    release_focus = function_body(source, "static void SDL3_ReleaseFocusInputState(int eventTime) {")
    clear_queues = function_body(source, "static void SDL3_ClearInputQueues(void) {")
    input_event = function_body(source, "static bool SDL3_IsUserInputEvent(Uint32 eventType) {")
    pump = function_body(source, "bool Sys_SDL_PumpEvents(void) {")
    poll_mouse = function_body(source, "int Sys_PollMouseInputEvents(void) {")

    for token in (
        'SDL_HINT_MOUSE_RELATIVE_MODE_CENTER, "1"',
        'SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "0"',
        'SDL_HINT_MOUSE_RELATIVE_WARP_MOTION, "0"',
        'SDL_HINT_MOUSE_TOUCH_EVENTS, "0"',
        'SDL_HINT_TOUCH_MOUSE_EVENTS, "0"',
    ):
        require(mouse_hints, token, "SDL3 mouse hint defaults")

    for token in (
        "SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false)",
        "SDL3_MapPhysicalScancode(scancode)",
    ):
        require(map_scancode, token, "SDL3 keyboard layout fallback")
    require(map_keycode, "keycode > 0 && keycode < K_BACKSPACE", "SDL3 printable keycode mapping")
    require(map_keycode, "K_GRAVE_A", "SDL3 Latin-1 keycode mapping")

    for token in ("K_BACKSPACE", "K_TAB", "K_ENTER", "SDL_KMOD_CTRL"):
        require(control_char, token, "SDL3 control character synthesis")

    for token in ("SDL_BUTTON_LEFT", "SDL_BUTTON_RIGHT", "SDL_BUTTON_MIDDLE", "SDL_BUTTON_X1", "SDL_BUTTON_X2", "button >= 6 && button <= 8"):
        require(map_mouse_button, token, "SDL3 mouse button mapping")

    require(queue_button, "Sys_QueEvent(eventTime, SE_KEY, key, down ? 1 : 0, 0, NULL);", "SDL3 mouse button event queue")
    require(queue_button, "SDL3_QueueMouseInput(M_ACTION1 + (key - K_MOUSE1)", "SDL3 mouse poll queue")
    require(source, "static const int SDL3_MAX_MOUSE_DELTA_PER_EVENT = 32767;", "SDL3 mouse delta cast guard")
    require(pump, "!std::isfinite(event.motion.x) || !std::isfinite(event.motion.y)", "SDL3 mouse motion finite guard")
    require(pump, "!std::isfinite(event.motion.xrel) || !std::isfinite(event.motion.yrel)", "SDL3 relative mouse motion finite guard")
    require(pump, "s_haveAbsoluteMousePosition = false;\n\t\t\t\t\tSDL3_ResetMenuMouseTracking();", "SDL3 malformed mouse motion state reset")
    require(consume_delta, "if (!std::isfinite(delta) || !std::isfinite(remainder))", "SDL3 mouse delta finite guard")
    require(consume_delta, "if (!std::isfinite(accumulated))", "SDL3 mouse accumulated delta finite guard")
    require(consume_delta, "idMath::ClampFloat(", "SDL3 mouse delta clamp")
    require(source, "static const int SDL3_MAX_MOUSE_WHEEL_STEPS_PER_EVENT = 64;", "SDL3 mouse wheel flood guard")
    require(pump, "if (!std::isfinite(deltaY))", "SDL3 mouse wheel finite guard")
    require(pump, "idMath::ClampFloat(", "SDL3 mouse wheel delta clamp")
    require(pump, "wheelSteps == idMath::INT_MIN", "SDL3 mouse wheel INT_MIN guard")
    require(pump, "queuedWheelSteps", "SDL3 mouse wheel clamped poll event")
    reject(pump, "abs(wheelSteps)", "SDL3 mouse wheel raw abs overflow guard")

    for token in (
        "SDL_SetWindowRelativeMouseMode(s_sdlWindow, true)",
        "SDL_SetWindowMouseGrab(s_sdlWindow, true)",
        "SDL_GetRelativeMouseState(NULL, NULL)",
    ):
        require(activate_mouse, token, "SDL3 relative mouse capture")
    require(source, 'cmdSystem->AddCommand("sdl3MouseCaptureDiagnostics"', "SDL3 mouse capture diagnostic command")
    require(mouse_capture_diag, "SDL3 mouse capture diagnostics: begin", "SDL3 mouse capture diagnostic output")
    require(mouse_capture_diag, "IN_ActivateMouse();", "SDL3 mouse capture diagnostic activation")
    require(mouse_capture_diag, "IN_DeactivateMouse();", "SDL3 mouse capture diagnostic cleanup")
    require(source, "relative=%s grab=%s captured=%s", "SDL3 mouse capture diagnostic state")
    require(input_frame, "if (!win32.activeApp)", "SDL3 fullscreen inactive-window capture gate")
    require(grab_mouse, "if (grabIt && !win32.activeApp)", "SDL3 per-frame inactive-window capture rejection")
    require(grab_mouse, "if (!grabIt)", "SDL3 explicit mouse release")
    require(grab_mouse, "IN_DeactivateMouse();", "SDL3 fullscreen/compositor mouse release")

    for token in (
        "SDL3_ClearInputQueues();",
        "idKeyInput::ClearStates();",
        "Sys_GrabMouseCursor(false);",
        "SDL3_StopControllerRumble();",
        "SDL3_ReleaseGamepadState(eventTime);",
        "SDL3_ReleaseJoystickState(eventTime);",
        "SDL3_ClearJoystickState();",
        "s_sdlFocusInputReleased",
    ):
        require(release_focus, token, "SDL3 desktop focus-loss input release")
    require(source, "if (s_sdlAppInBackground || !s_sdlVideoReferenceHeld", "SDL3 lifecycle-aware focus query")
    reject(clear_queues, "s_menuMouseInsideWindow = true", "SDL3 input-queue clear preserving compositor mouse focus")
    require(window_event, "case SDL_EVENT_WINDOW_FOCUS_LOST:", "SDL3 focus-loss handling")
    require(window_event, "case SDL_EVENT_WINDOW_HIDDEN:", "SDL3 hidden-window input release")
    require(window_event, "case SDL_EVENT_WINDOW_MINIMIZED:", "SDL3 minimize handling")
    require(window_event, "case SDL_EVENT_WINDOW_SHOWN:", "SDL3 shown-window focus recheck")
    require(window_event, "case SDL_EVENT_WINDOW_RESTORED:", "SDL3 restored-window focus recheck")
    require(window_event, "win32.activeApp = Sys_SDL_IsGameWindowFocused();", "SDL3 shown/restored focus query")
    if window_event.count("SDL3_ReleaseFocusInputState(eventTime);") < 2:
        raise AssertionError("SDL3 focus loss and hidden/minimized windows must release latched input")
    for token in (
        "SDL_EVENT_KEY_DOWN",
        "SDL_EVENT_MOUSE_MOTION",
        "SDL_EVENT_GAMEPAD_BUTTON_DOWN",
        "SDL_EVENT_JOYSTICK_BUTTON_DOWN",
        "SDL_EVENT_FINGER_DOWN",
    ):
        require(input_event, token, "SDL3 inactive-window input classification")
    require(pump, "!win32.activeApp && SDL3_IsUserInputEvent(event.type)", "SDL3 inactive-window input rejection")

    require(video_hints, 'SDL_HINT_IME_IMPLEMENTED_UI, "none"', "SDL3 native IME UI contract")
    require(init_input, "SDL3_UpdateTextInputState();", "SDL3 text input initial state")
    require(text_input_state, "consoleAcceptsText || guiAcceptsText", "SDL3 focused UI text-input routing")
    require(text_input_state, "SDL3_GetActiveGuiTextInputState", "SDL3 focused edit-field query")
    require(text_input_state, "SDL_StartTextInput(s_sdlWindow)", "SDL3 text input activation")
    require(text_input_state, "SDL_ClearComposition(s_sdlWindow)", "SDL3 IME composition dismissal")
    require(text_input_state, "SDL_StopTextInput(s_sdlWindow)", "SDL3 text input deactivation")
    require(text_input_area, "SDL_SetTextInputArea(s_sdlWindow", "SDL3 native IME candidate placement")
    require(text_input_area, "transform.pixelToWindowX", "SDL3 high-DPI candidate placement")
    require(pump, "SDL3_UpdateTextInputState();", "SDL3 per-pump text input lifecycle")
    require(pump, "SDL_StepUTF8(&text, &remaining)", "SDL3 strict committed UTF-8 decoding")
    require(pump, "codepoint == SDL_INVALID_UNICODE_CODEPOINT || codepoint > 0xff", "SDL3 stock-font Unicode narrowing guard")
    require(pump, "idStr::CharIsPrintable(static_cast<byte>(codepoint))", "SDL3 committed control-character rejection")
    require(pump, "ignoring committed text outside the stock single-byte font range", "SDL3 unsupported text diagnostic")
    for token in (
        "SDL_EVENT_KEY_DOWN",
        "SDL_EVENT_KEY_UP",
        "SDL_EVENT_TEXT_INPUT",
        "SDL_EVENT_TEXT_EDITING",
        "SDL_EVENT_TEXT_EDITING_CANDIDATES",
        "SDL_EVENT_MOUSE_MOTION",
        "SDL_EVENT_MOUSE_BUTTON_DOWN",
        "SDL_EVENT_MOUSE_BUTTON_UP",
        "SDL_EVENT_MOUSE_WHEEL",
        "SDL_StepUTF8",
        "SDL3_ConsumeMouseDelta(event.motion.xrel, s_sdlRelativeMouseRemainderX)",
        "openQ4_AcceptingLoadingContinueInput()",
    ):
        require(pump, token, "SDL3 keyboard/mouse event pump")

    require(poll_mouse, "Sys_SDL_PumpEvents();", "POSIX mouse poll pumps SDL events")
    require(poll_mouse, "#if defined(OPENQ4_SDL3_POSIX_HOST)", "POSIX-only mouse poll pump")


def validate_controller_contract() -> None:
    source = read("src/sys/sdl3/sdl3_backend.cpp")
    controller_hints = function_body(source, "static void SDL3_SetControllerHintDefaults(void) {")
    init_controllers = function_body(source, "static void SDL3_InitControllerSubsystems(void) {")
    list_controllers = function_body(source, "static void SDL3_ListControllers_f(const idCmdArgs &args) {")
    open_all = function_body(source, "static void SDL3_OpenAllControllers(void) {")
    open_controller = function_body(source, "static bool SDL3_OpenController(SDL_JoystickID id, int eventTime) {")
    close_controller = function_body(source, "static void SDL3_CloseController(SDL_JoystickID id, int eventTime) {")
    activate_controller = function_body(source, "static void SDL3_ActivateController(SDL_JoystickID id, int eventTime) {")
    deactivate_controller = function_body(source, "static void SDL3_DeactivateController(int eventTime) {")
    apply_change = function_body(source, "static void SDL3_ApplyControllerChange(openq4::ControllerChange change, int eventTime) {")
    note_use = function_body(source, "static void SDL3_NoteControllerUse(SDL_JoystickID id, int eventTime) {")
    gamepad_axes = function_body(source, "static void SDL3_UpdateGamepadAxes(int eventTime) {")
    joystick_axes = function_body(source, "static void SDL3_UpdateJoystickAxes(void) {")
    map_gamepad_button = function_body(source, "static int SDL3_MapGamepadButton(Uint8 button) {")
    joy_from_ordinal = function_body(source, "static int SDL3_JoyKeyFromOrdinal(int ordinal) {")
    set_hat = function_body(source, "static void SDL3_SetJoystickHat(Uint8 newHat, int eventTime) {")
    gyro = function_body(source, "static void SDL3_HandleGamepadSensorEvent(const SDL_GamepadSensorEvent &event, int eventTime) {")
    touchpad = function_body(source, "static void SDL3_HandleGamepadTouchpadEvent(const SDL_GamepadTouchpadEvent &event, int eventTime) {")
    finger = function_body(source, "static void SDL3_HandleFingerEvent(const SDL_TouchFingerEvent &event, int eventTime) {")
    clamp_range = function_body(source, "static float SDL3_ClampRange(float value, float minValue, float maxValue) {")
    clamp_unit = function_body(source, "static float SDL3_ClampUnit(float value) {")
    clamp_rumble = function_body(source, "static Uint16 SDL3_ClampRumbleValue(float value) {")
    axis_float = function_body(source, "static int SDL3_AxisFloatToJoystickValue(float value) {")
    rumble = function_body(source, "bool Sys_SetJoystickRumble(float lowFrequency, float highFrequency, int durationMsec) {")
    background = function_body(source, "static void SDL3_HandleAppBackgroundTransition(int eventTime, const char *reason) {")
    foreground = function_body(source, "static void SDL3_HandleAppForegroundTransition(int eventTime, const char *reason) {")
    pump = function_body(source, "bool Sys_SDL_PumpEvents(void) {")

    for token in (
        'SDL_HINT_JOYSTICK_HIDAPI, "1"',
        'SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "auto"',
        'SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0"',
        'SDL_HINT_JOYSTICK_HIDAPI_PS4, "1"',
        'SDL_HINT_JOYSTICK_HIDAPI_PS5, "1"',
        'SDL_HINT_JOYSTICK_HIDAPI_STEAM, "1"',
        'SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1"',
        'SDL_HINT_JOYSTICK_HIDAPI_SWITCH2, "1"',
        'SDL_HINT_JOYSTICK_HIDAPI_XBOX, "1"',
        'SDL_HINT_HIDAPI_UDEV, "1"',
        'SDL_HINT_JOYSTICK_LINUX_CLASSIC, "0"',
        'SDL_HINT_JOYSTICK_LINUX_DEADZONES, "0"',
        'SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK, "1"',
        'SDL_HINT_JOYSTICK_IOKIT, "1"',
        'SDL_HINT_JOYSTICK_MFI, "1"',
    ):
        require(controller_hints, token, "SDL3 POSIX controller hint defaults")
    require(controller_hints, "#if defined(OPENQ4_SDL3_POSIX_HOST)", "SDL3 controller hints stay POSIX-scoped")
    require(controller_hints, "#if defined(OPENQ4_SDL3_LINUX_HOST)", "SDL3 Linux controller hints")
    require(controller_hints, "#if defined(OPENQ4_SDL3_DARWIN_HOST)", "SDL3 macOS controller hints")

    require_order(init_controllers, "SDL3_SetControllerHintDefaults();", "SDL_InitSubSystem(SDL_INIT_GAMEPAD)", "controller hints before gamepad init")
    require(init_controllers, "SDL_InitSubSystem(SDL_INIT_GAMEPAD)", "SDL3 gamepad subsystem init")
    require(init_controllers, "SDL_SetGamepadEventsEnabled(true)", "SDL3 gamepad event enable")
    require(init_controllers, "SDL_InitSubSystem(SDL_INIT_JOYSTICK)", "SDL3 joystick subsystem init")
    require(init_controllers, "SDL_SetJoystickEventsEnabled(true)", "SDL3 joystick event enable")
    require(init_controllers, "SDL3_OpenAllControllers();", "SDL3 opens every controller")

    for token in (
        "SDL controller hints:",
        "SDL Linux controller hints:",
        "SDL macOS controller hints:",
        "SDL_HINT_JOYSTICK_HIDAPI",
        "SDL_HINT_JOYSTICK_ENHANCED_REPORTS",
        "SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK",
        "SDL_HINT_JOYSTICK_IOKIT",
        "SDL_HINT_JOYSTICK_MFI",
    ):
        require(list_controllers, token, "SDL3 controller diagnostics")

    require(open_all, "SDL_GetGamepads(&gamepadCount)", "SDL3 gamepad enumeration")
    require(open_all, "SDL_GetJoysticks(&joystickCount)", "SDL3 joystick fallback enumeration")
    require_order(open_all, "SDL_GetGamepads(&gamepadCount)", "SDL_GetJoysticks(&joystickCount)", "prefer SDL gamepads before raw joysticks")
    require(open_all, "if (!SDL_IsGamepad(joysticks[i]))", "SDL3 joystick fallback excludes gamepads")
    require(open_all, "s_controllerRoster.SetPreference(in_joystickDevice.GetString())", "PAD-01 saved device choice")
    reject(open_all, "break;", "PAD-01: every controller opens, not the first")
    require(open_controller, "SDL_OpenGamepad(id)", "SDL3 gamepad open")
    require(open_controller, "SDL_OpenJoystick(id)", "SDL3 joystick open")
    require(open_controller, "if (SDL_IsGamepad(id))", "SDL3 joystick fallback excludes gamepads")
    require(open_controller, "s_controllerRoster.Add(", "PAD-01: the roster decides who takes charge")
    require(open_controller, "SDL3_ApplyControllerChange(change, eventTime);", "PAD-01: the roster decides who takes charge")
    require(activate_controller, "SDL3_UpdateGamepadSensorState(true)", "SDL3 gamepad sensor capability update")
    require(activate_controller, "in_gamepadFamily.SetString(SDL3_GamepadFamily(controller->pad));", "PAD-01: prompts follow the active controller")
    # PAD-01: the outgoing controller's held state releases before the new one speaks
    for token in ("SDL3_StopControllerRumble();", "SDL3_ReleaseGamepadState(eventTime);", "SDL3_ReleaseJoystickState(eventTime);",
                  "SDL_SetGamepadSensorEnabled(s_sdlGamepad, SDL_SENSOR_GYRO, false)", "SDL3_ClearJoystickState();"):
        require(deactivate_controller, token, "PAD-01 release on handover")
    reject(deactivate_controller, "SDL_CloseGamepad", "PAD-01: a handover leaves the device open")
    require_order(apply_change, "SDL3_DeactivateController(eventTime);", "SDL3_ActivateController(s_controllerRoster.Active(), eventTime);", "PAD-01 release before handover")
    require(apply_change, 'in_gamepadFamily.SetString("none");', "PAD-01: a loss with no successor reaches the notice")
    require(close_controller, "s_controllerRoster.Remove(", "PAD-01: the roster picks the successor")
    require(note_use, "s_controllerRoster.NoteUse(", "PAD-01 use-to-switch")
    require(note_use, "SDL3_ActiveControllerHolding()", "PAD-01: charge never moves from a controller being held")
    require(clamp_range, "if (!std::isfinite(value))", "SDL3 controller float clamp finite guard")
    require(clamp_unit, "return SDL3_ClampRange(value, 0.0f, 1.0f);", "SDL3 unit clamp finite guard")
    require(clamp_rumble, "if (!std::isfinite(value) || value <= 0.0f)", "SDL3 rumble value finite guard")
    require(axis_float, "if (!std::isfinite(value))", "SDL3 joystick float conversion finite guard")

    for token in (
        "SDL_GAMEPAD_AXIS_LEFTX",
        "SDL_GAMEPAD_AXIS_RIGHTX",
        "in_joystickSouthpaw",
        "in_joystickInvertLook",
        "SDL_GAMEPAD_AXIS_LEFT_TRIGGER",
        "SDL_GAMEPAD_AXIS_RIGHT_TRIGGER",
        "SDL3_UpdateTriggerButtons(leftTrigger, rightTrigger, eventTime)",
        "s_joystickAxisState[AXIS_ROLL] = 127",
    ):
        require(gamepad_axes, token, "SDL3 gamepad axes")

    for token in (
        "SDL_GetNumJoystickAxes(s_sdlJoystick)",
        "SDL3_ShouldUseDedicatedJoystickLookAxes",
        "in_joystickMoveAxisX",
        "in_joystickLookAxisX",
        "in_joystickUpAxis",
        "s_joystickAxisState[AXIS_ROLL] = hasDedicatedLookAxis ? 127 : 0",
    ):
        require(joystick_axes, token, "SDL3 generic joystick axes")

    for token in (
        "SDL_GAMEPAD_BUTTON_LEFT_SHOULDER",
        "SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER",
        "SDL_GAMEPAD_BUTTON_SOUTH",
        "SDL_GAMEPAD_BUTTON_EAST",
        "SDL_GAMEPAD_BUTTON_TOUCHPAD",
        "SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1",
        "SDL_GAMEPAD_BUTTON_MISC6",
    ):
        require(map_gamepad_button, token, "SDL3 gamepad button map")
    require(joy_from_ordinal, "s_joyKeys", "SDL3 joystick button map")
    require(joy_from_ordinal, "s_auxKeys", "SDL3 joystick aux button map")
    require(set_hat, "SDL_HAT_UP", "SDL3 joystick hat map")
    require(set_hat, "K_JOY12", "SDL3 joystick hat map")

    for token in (
        "SDL_GamepadHasSensor(s_sdlGamepad, SDL_SENSOR_GYRO)",
        "SDL_SetGamepadSensorEnabled(s_sdlGamepad, SDL_SENSOR_GYRO, wantsGyro)",
        "SDL_EVENT_GAMEPAD_SENSOR_UPDATE",
        "SDL3_QueueMouseDelta(dx, dy, eventTime);",
    ):
        require(source, token, "SDL3 gyro support")
    reject(gyro, "!SDL3_IsMouseCaptured()", "PAD-04: gyro aim does not wait for the mouse to be captured")
    require(gyro, "SDL3_ShouldRouteMenuMouse()", "SDL3 gyro menu suppression")
    # PAD-04: calibration, filtering, the turning space and the gyro's own
    # ratio, in that order; play receives degrees of view, not mouse counts.
    for first, second in (
        ("openq4::UpdateGyroCalibration(s_gyroCalibration, raw,", "openq4::ApplyGyroCalibration(s_gyroCalibration, raw)"),
        ("openq4::ApplyGyroCalibration(s_gyroCalibration, raw)", "openq4::FilterGyro(s_gyroFilter, calibrated, deltaSeconds, filterSettings)"),
        ("openq4::FilterGyro(s_gyroFilter, calibrated, deltaSeconds, filterSettings)", "openq4::ComputeGyroTurn(filtered, s_gyroGravity, in_gyroSpace.GetInteger())"),
        ("openq4::ComputeGyroTurn(filtered, s_gyroGravity, in_gyroSpace.GetInteger())", "s_gyroLookYaw = idMath::ClampFloat("),
    ):
        require_order(gyro, first, second, "PAD-04 gyro sample path")
    for token in ("in_gyroYawRatio.GetFloat()", "in_gyroPitchRatio.GetFloat()", "in_gyroInvertYaw.GetBool()", "in_gyroInvertPitch.GetBool()",
                  "in_gyroDeadZone.GetFloat()", "in_gyroSmoothing.GetFloat()", "SDL3_StoreGyroCalibration(s_gyroIdentity, s_gyroCalibration.offset);"):
        require(gyro, token, "PAD-04 gyro settings")
    for token in ('cvarSystem->GetCVarFloat("sensitivity")', "m_cpi", "m_smooth", "m_filter", "cl_mouseAccel", "RADIANS_TO_CLASSIC_MOUSE_COUNTS"):
        reject(gyro, token, "PAD-04: no mouse setting touches gyro aim")
    look = function_body(source, "bool Sys_GetGyroLook(float &yaw, float &pitch, bool consume) {")
    require(look, "if (consume) {", "PAD-04: the late-look path peeks without taking")
    usercmd = read("src/framework/UsercmdGen.cpp")
    gyro_move = function_body(usercmd, "void idUsercmdGenLocal::GyroMove( void ) {")
    require_order(gyro_move, "Sys_GetGyroLook( yaw, pitch, true )", "GyroLookActive()", "PAD-04: motion made while the gyro is off is dropped, not saved")
    require(gyro_move, "GetZoomLookSensitivityScale()", "PAD-04: zoom scales gyro aim")
    for token in ("sensitivity.GetFloat()", "m_yaw", "m_pitch", "NormalizeMouseDeltasForCpi", "CalculateMouseSensitivity", "mouseDx"):
        reject(gyro_move, token, "PAD-04: no mouse setting touches gyro aim")
    require(usercmd, '{ "_gyro",\t\t\tUB_GYRO },', "PAD-04 _gyro binding")

    for token in (
        "SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN",
        "SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION",
        "SDL_EVENT_GAMEPAD_TOUCHPAD_UP",
        "mode == 1",
        "mode == 2",
        "SDL3_QueueMouseDelta(dx, dy, eventTime);",
    ):
        require(touchpad if "mode ==" in token or "QueueMouseDelta" in token else pump, token, "SDL3 gamepad touchpad support")

    for token in (
        "SDL_EVENT_FINGER_DOWN",
        "SDL_EVENT_FINGER_MOTION",
        "SDL_EVENT_FINGER_UP",
        "SDL_EVENT_FINGER_CANCELED",
    ):
        require(pump, token, "SDL3 touchscreen event pump")
    require(finger, "in_touchscreen.GetBool()", "SDL3 touchscreen cvar")
    require(finger, "SDL3_SetRoutedCursorFromWindowPosition", "SDL3 touchscreen cursor routing")
    require(finger, "openQ4_AcceptingLoadingContinueInput()", "SDL3 touchscreen loading-continue routing")

    for token in (
        "SDL_RumbleGamepad",
        "SDL_RumbleJoystick",
        "SDL3_GetEffectiveRumbleScale",
        "SDL3_SendControllerRumble",
    ):
        require(source if token.startswith("SDL_Rumble") else rumble, token, "SDL3 rumble support")

    for token in (
        "SDL_EVENT_GAMEPAD_ADDED",
        "SDL_EVENT_GAMEPAD_REMOVED",
        "SDL_EVENT_JOYSTICK_ADDED",
        "SDL_EVENT_JOYSTICK_REMOVED",
        "SDL_EVENT_GAMEPAD_BUTTON_DOWN",
        "SDL_EVENT_GAMEPAD_AXIS_MOTION",
        "SDL_EVENT_JOYSTICK_BUTTON_DOWN",
        "SDL_EVENT_JOYSTICK_HAT_MOTION",
        "SDL_EVENT_JOYSTICK_AXIS_MOTION",
    ):
        require(pump, token, "SDL3 controller event pump")

    # PAD-01: a press, or a stick or trigger past its dead zone, on any controller
    # (the active one's use tells the roster how long it has rested)
    for token in (
        "SDL3_NoteControllerUse(event.gbutton.which, eventTime);",
        "SDL3_GamepadAxisIsUse(source->pad, event.gaxis.axis)",
        "SDL3_NoteControllerUse(event.jbutton.which, eventTime);",
        "SDL3_JoystickAxisIsUse(source->joystick)",
        "SDL_EVENT_JOYSTICK_BATTERY_UPDATED",
        "(void)SDL3_OpenController(event.gdevice.which, eventTime);",
        "SDL3_CloseController(event.gdevice.which, eventTime);",
    ):
        require(pump, token, "PAD-01 controller event pump")
    require_order(pump, "SDL3_NoteControllerUse(event.gbutton.which, eventTime);", "SDL3_PostControllerKeyEvent(SDL3_MapGamepadButton(event.gbutton.button), down, eventTime);",
                  "PAD-01: the press that hands charge over still counts")

    require(background, "SDL3_ReleaseFocusInputState(eventTime);", "SDL3 background input release")
    for token in (
        "SDL3_OpenAllControllers();",
        "SDL3_UpdateGamepadSensorState(false);",
        "SDL3_UpdateGamepadAxes(eventTime);",
        "SDL3_UpdateJoystickAxes();",
        "Sys_GrabMouseCursor(true);",
    ):
        require(foreground, token, "SDL3 foreground input reacquire")


def validate_docs_and_ci() -> None:
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")
    runner = read("tools/validation/openq4_validate.py")
    input_docs = read("docs/user/input-settings.md")
    platform_docs = read("docs/dev/platform-support.md")
    migration = read("docs/dev/archive/2026-10-18/sdl3-linux-macos-migration.md")
    release_completion = read("docs/dev/release-completion.md")
    session = read("src/framework/Session.cpp")

    for haystack, context in (
        (push, "push verification workflow"),
        (commit, "commit validation workflow"),
        (runner, "validation Python tests"),
    ):
        require(haystack, "sdl3_input_parity.py", context)

    for token in (
        "Controller support is first-class in SDL3 builds on Windows, Linux, and macOS.",
        "Linux and macOS SDL3 builds opt into SDL's HIDAPI controller backends",
        "listControllers",
    ):
        require(input_docs, token, "input settings docs")

    require(platform_docs, "keyboard, mouse, and controller input are routed through the shared SDL3 backend", "platform support docs")
    require(migration, "keyboard, mouse, controller, rumble, hotplug, gyro, touchpad, and touchscreen handling", "SDL3 migration docs")
    require(release_completion, "SDL3 input support is aligned across Windows, Linux, and macOS", "release completion notes")
    require(session, "Sys_SDL_IsGameWindowFocused", "SDL3 unfocused-audio policy")
    require(session, "s_muteUnfocused.GetBool() && !Sys_SDL_IsGameWindowFocused()", "SDL3 cross-platform unfocused-audio mute")


def main() -> None:
    validate_shared_backend_sources()
    validate_keyboard_and_mouse_contract()
    validate_controller_contract()
    validate_docs_and_ci()
    print("sdl3_input_parity: ok")


if __name__ == "__main__":
    main()
