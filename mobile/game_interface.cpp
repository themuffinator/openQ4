// Android/SigmaTouch support by emileb: https://github.com/emileb/openQ4/tree/android
// Integrated and adapted for current openQ4; licensed under GPL-3.0-or-later.

// Quake 4's Portable* implementation. See docs/engines/quake4.md in Sigma Touch.
//
// Nothing here includes an engine header: everything that has to touch engine
// state goes through mobile/quake4_bridge.h, whose implementation lives in
// sys/android/android_sdl3.cpp and only ever runs on the engine thread.

#include <pthread.h>
#include <errno.h>
#include <cmath>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <android/log.h>

#include <SDL3/SDL_scancode.h>

#include "game_interface.h"
#include "quake4_bridge.h"

#define LOG_TAG "openQ4"
#define Q4_LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))

// Set by the shared JNI glue (Clibs_OpenTouch/android_jni_inc.cpp) before it
// calls PortableInit. The engine needs it to find its sibling modules.
extern const char *nativeLibsPath;

// -------------------------------------------------------------------------
// Startup
// -------------------------------------------------------------------------

// Third-party code (SDL, OpenAL, the C runtime) writes to stdout/stderr, where
// it would otherwise vanish. The engine itself no longer comes through here:
// Sys_Printf writes to liblog directly, because a pipe can only carry bytes and
// this pump would have to guess where the engine's lines began and ended. See
// sys/android/android_log.cpp.
//
// A read() returns whatever bytes happen to be in flight, so it can deliver
// several lines at once or stop halfway through one. Reassembling here is what
// keeps one printed line equal to one logcat record.
#define STDIO_PUMP_LINE_MAX 1008

static void *stdio_pump(void *arg)
{
    int fd = (int) (long) arg;
    char line[STDIO_PUMP_LINE_MAX + 1];
    int len = 0;
    char buf[512];
    ssize_t n;

    while (true)
    {
        n = read(fd, buf, sizeof(buf));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        for (ssize_t i = 0; i < n; i++)
        {
            if (buf[i] == '\n' || len == STDIO_PUMP_LINE_MAX)
            {
                if (len > 0)
                {
                    line[len] = 0;
                    Q4_LOGI("%s", line);
                    len = 0;
                }

                if (buf[i] == '\n')
                    continue;
            }

            if (buf[i] != '\r')
                line[len++] = buf[i];
        }
    }

    if (len > 0)
    {
        line[len] = 0;
        Q4_LOGI("%s", line);
    }
    close(fd);
    return NULL;
}

static void redirect_stdio_to_logcat()
{
    int pipes[2];
    pthread_t thread;

    if (pipe(pipes) != 0)
        return;

    // Start the reader before redirecting: a failed thread creation must not
    // leave stdout/stderr connected to a pipe nobody will ever drain.
    if (pthread_create(&thread, NULL, stdio_pump, (void *) (long) pipes[0]) != 0)
    {
        close(pipes[0]);
        close(pipes[1]);
        return;
    }
    pthread_detach(thread);
    dup2(pipes[1], STDOUT_FILENO);
    dup2(pipes[1], STDERR_FILENO);
    close(pipes[1]); // fd 1 and 2 are the write end now

    // Must follow the dup2: setvbuf only has to be honoured before a stream's
    // first I/O, and a fully buffered stdout would hold up to 4 KB of output
    // until a flush that a crash never performs.
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

}

void PortableInit(int argc, const char **argv)
{
    redirect_stdio_to_logcat();

    if (nativeLibsPath && nativeLibsPath[0])
        setenv("OPENQ4_NATIVE_LIBS", nativeLibsPath, 1);

    Q4_LOGI("PortableInit, starting engine");

    OpenQ4_AndroidMain(argc, argv); // never returns
}

// -------------------------------------------------------------------------
// Touch thread -> engine thread queue
//
// Every Portable* call below runs on Android's UI thread. All they may do is
// push here (or update mutex-protected axis levels); Quake4_DrainTouchInput,
// called once per event pump from the engine thread, is what actually talks to
// the engine.
// -------------------------------------------------------------------------

enum
{
    EV_KEY,          // a = SDL scancode, b = down
    EV_CHAR,         // a = codepoint
    EV_MOUSE_DELTA,  // a = dx, b = dy
    EV_MOUSE_BUTTON, // a = button, b = down
    EV_IMPULSE,      // a = impulse number
    EV_BUTTON,       // a = QUAKE4_BTN_*, b = down
    EV_COMMAND       // command = owned text, retained until the engine drains it
};

#define EVENT_QUEUE_SIZE 128
#define COMMAND_MAX_LEN 128

struct TouchEvent
{
    int type;
    int a;
    int b;
    char command[COMMAND_MAX_LEN];
};

static TouchEvent eventQueue[EVENT_QUEUE_SIZE];
static int eventHead, eventTail;
static std::mutex inputMutex;
static bool resetPending;
static unsigned int heldButtonSources[QUAKE4_BTN_COUNT];

// Protected by inputMutex, including read/modify/write of fractional look.
static float s_moveStick, s_strafeStick;
static int s_moveDigital, s_strafeDigital;
static bool s_forwardDown, s_backDown, s_leftDown, s_rightDown;
static bool s_turnLeftDown, s_turnRightDown;
static float s_yawJoy, s_pitchJoy;
static float s_yawMouse, s_pitchMouse;

static void clearPendingInput()
{
    eventHead = eventTail = 0;
    s_moveStick = s_strafeStick = 0.0f;
    s_moveDigital = s_strafeDigital = 0;
    s_forwardDown = s_backDown = s_leftDown = s_rightDown = false;
    s_turnLeftDown = s_turnRightDown = false;
    s_yawJoy = s_pitchJoy = s_yawMouse = s_pitchMouse = 0.0f;
    memset(heldButtonSources, 0, sizeof(heldButtonSources));
}

extern "C" void Quake4_ClearTouchInput(void)
{
    std::lock_guard<std::mutex> lock(inputMutex);
    clearPendingInput();
    resetPending = true;
}

// inputMutex is held by the caller, including source-state updates below.
static void queueEventLocked(int type, int a, int b, const char *command = NULL)
{
    const int next = (eventHead + 1) % EVENT_QUEUE_SIZE;
    if (next == eventTail)
    {
        // A lost release would leave a key or attack held forever. Discard the
        // incomplete batch and release previously delivered touch input.
        clearPendingInput();
        resetPending = true;
        return;
    }
    TouchEvent &event = eventQueue[eventHead];
    event.type = type;
    event.a = a;
    event.b = b;
    event.command[0] = 0;
    if (command)
    {
        strncpy(event.command, command, COMMAND_MAX_LEN - 1);
        event.command[COMMAND_MAX_LEN - 1] = 0;
    }
    eventHead = next;
}

static void queueEvent(int type, int a, int b, const char *command = NULL)
{
    std::lock_guard<std::mutex> lock(inputMutex);
    queueEventLocked(type, a, b, command);
}

static void queueButton(int button, unsigned int source, int down)
{
    std::lock_guard<std::mutex> lock(inputMutex);
    const unsigned int before = heldButtonSources[button];
    const unsigned int after = down ? before | source : before & ~source;
    heldButtonSources[button] = after;
    if ((before != 0) != (after != 0))
        queueEventLocked(EV_BUTTON, button, after != 0);
}

static void queueCommand(const char *cmd)
{
    if (cmd && cmd[0] && strlen(cmd) < COMMAND_MAX_LEN)
        queueEvent(EV_COMMAND, 0, 0, cmd);
}

// A screen-fraction swipe is worth about this many mouse pixels. Matches the
// feel of the other OpenTouch ports at the default in-game sensitivity.
#define LOOK_MOUSE_YAW_SCALE   3000.0f
#define LOOK_MOUSE_PITCH_SCALE 2000.0f

// The touch layer's sticks come in pre-scaled well past unity (leftStick alone
// multiplies by 15 and 10), so the levels are clamped to -1..1 as they arrive
// and only turned into engine axis units here.
static float clampUnit(float v)
{
    if (!std::isfinite(v))
        return 0.0f;
    if (v > 1.0f)
        return 1.0f;
    if (v < -1.0f)
        return -1.0f;
    return v;
}

// idMath::ClampChar territory: 127 is the engine's full-speed / full-rate value,
// the same one a held movement key produces.
#define AXIS_MAX 127

static int axisValue(float unit)
{
    return (int)(unit * AXIS_MAX);
}

extern "C" void Quake4_DrainTouchInput(void)
{
    TouchEvent events[EVENT_QUEUE_SIZE];
    int eventCount = 0;
    float moveStick, strafeStick, yawJoy, pitchJoy;
    int moveDigital, strafeDigital, dx, dy;
    bool reset;
    {
        std::lock_guard<std::mutex> lock(inputMutex);
        reset = resetPending;
        resetPending = false;
        while (eventTail != eventHead)
        {
            events[eventCount++] = eventQueue[eventTail];
            eventTail = (eventTail + 1) % EVENT_QUEUE_SIZE;
        }
        moveStick = s_moveStick;
        strafeStick = s_strafeStick;
        moveDigital = s_moveDigital;
        strafeDigital = s_strafeDigital;
        yawJoy = s_turnRightDown || s_turnLeftDown
            ? float(s_turnRightDown) - float(s_turnLeftDown) : s_yawJoy;
        pitchJoy = s_pitchJoy;
        dx = (int)s_yawMouse;
        dy = (int)s_pitchMouse;
        s_yawMouse -= (float)dx;
        s_pitchMouse -= (float)dy;
    }
    if (reset)
        Quake4_ResetTouchState();
    for (int i = 0; i < eventCount; ++i)
    {
        const TouchEvent &ev = events[i];
        switch (ev.type)
        {
            case EV_KEY:          Quake4_PostKey(ev.a, ev.b); break;
            case EV_CHAR:         Quake4_PostChar(ev.a); break;
            case EV_MOUSE_DELTA:  Quake4_PostMouseDelta(ev.a, ev.b); break;
            case EV_MOUSE_BUTTON: Quake4_PostMouseButton(ev.a, ev.b); break;
            case EV_IMPULSE:      Quake4_TriggerImpulse(ev.a); break;
            case EV_BUTTON:       Quake4_PostButton(ev.a, ev.b); break;
            case EV_COMMAND:      Quake4_PostCommand(ev.command); break;
        }
    }

    // AXIS_ROLL is a capability flag, not an axis: non-zero tells JoystickMove
    // there is a dedicated look stick. That also decides which axes mean what,
    // and the roles are the opposite way round from the names - look comes from
    // AXIS_SIDE/AXIS_FORWARD, movement from AXIS_YAW/AXIS_PITCH.
    Quake4_PostJoystickAxis(QUAKE4_AXIS_ROLL, 1);

    // Movement. The dpad wins over the stick, so a player using both does not
    // get half speed from a stick resting at zero. Positive is right/forward,
    // which is what cmd.rightmove / cmd.forwardmove want.
    Quake4_PostJoystickAxis(QUAKE4_AXIS_YAW,
                            strafeDigital ? strafeDigital * AXIS_MAX : axisValue(strafeStick));
    Quake4_PostJoystickAxis(QUAKE4_AXIS_PITCH,
                            moveDigital ? moveDigital * AXIS_MAX : axisValue(moveStick));

    // Look, only used when the player turns on joystick look mode; the default
    // is the mouse path below. JoystickMove does `viewangles[YAW] -= lookAxisX`
    // and `viewangles[PITCH] += lookAxisY`, and the touch layer has already
    // negated pitch relative to its mouse value, hence the signs.
    Quake4_PostJoystickAxis(QUAKE4_AXIS_SIDE, -axisValue(yawJoy));
    Quake4_PostJoystickAxis(QUAKE4_AXIS_FORWARD, axisValue(pitchJoy));

    // Take the whole counts and leave the remainder to be carried into the
    // next frame, so nothing is lost between drains.
    if (dx || dy)
    {
        Quake4_PostMouseDelta(-dx, -dy);
    }
}

// -------------------------------------------------------------------------
// Portable* API
// -------------------------------------------------------------------------

void PortableBackButton(void)
{
    // default.cfg binds ESCAPE to a "togglemenu" command this engine does not
    // register; Session::ProcessEvent handles the raw K_ESCAPE key instead.
    queueEvent(EV_KEY, SDL_SCANCODE_ESCAPE, 1);
    queueEvent(EV_KEY, SDL_SCANCODE_ESCAPE, 0);
}

int PortableKeyEvent(int state, int code, int unitcode)
{
    // The shared touch layer (keyboard, blank-screen tap) sends SDL scancodes.
    queueEvent(EV_KEY, code, state);

    if (state && unitcode > 0)
        queueEvent(EV_CHAR, unitcode, 0);

    return 0;
}

void PortableAction(int state, int action)
{
    // Custom buttons are keys the player binds in Quake 4's own controls menu,
    // so they have to reach the engine in either mode - pressing one while the
    // bind screen is up is the whole point. The scheme is the shared one the
    // touch layer's own labels name: KP1-KP0 for the ten buttons, A-P for the
    // four quad slides. SDL's KP_1..KP_9 are followed by KP_0, hence the single
    // run. Nothing is unbound-safe here: A-P land on Quake 4's default WASD
    // binds until the player rebinds them.
    if (action >= PORT_ACT_CUSTOM_0 && action <= PORT_ACT_CUSTOM_25)
    {
        if (action <= PORT_ACT_CUSTOM_9)
            queueEvent(EV_KEY, SDL_SCANCODE_KP_1 + action - PORT_ACT_CUSTOM_0, state);
        else
            queueEvent(EV_KEY, SDL_SCANCODE_A + action - PORT_ACT_CUSTOM_10, state);

        return;
    }

    // Menu actions only do anything while a menu is up - the same split
    // gzdoom_game_interface.cpp uses. In game those scancodes are live binds
    // (default.cfg has UPARROW as _forward), so a gamepad dpad or the yes/no
    // screen would otherwise drive the player around. The console counts as
    // menu here: it borrows the menu overlay and reads the arrows and escape
    // itself.
    const bool menuUp = PortableGetScreenMode() != TS_GAME;

    // Overlay buttons that exist in both modes, so neither branch may swallow
    // them: the console toggle sits on the menu overlay, quick save/load on the
    // gamepad utility one.
    switch (action)
    {
        // Stays a raw key: the console reads the key itself, ahead of any bind.
        case PORT_ACT_CONSOLE:   queueEvent(EV_KEY, SDL_SCANCODE_GRAVE, state); return;

        case PORT_ACT_QUICKSAVE: if (state) queueCommand("savegame quick"); return;
        case PORT_ACT_QUICKLOAD: if (state) queueCommand("loadgame quick"); return;

        default: break;
    }

    // Releases are handled whatever the mode is: a menu that closes on the press
    // must not leave its key stuck down in the game that follows.
    if (menuUp || !state)
    {
        switch (action)
        {
            // Menu and console navigation reads raw keys rather than going
            // through the rebindable path, so synthetic key presses are both
            // correct and simplest. The menus themselves are worked with the
            // pointer (see TouchInterface::mouseMove); these are what the
            // gamepad and the yes/no screen send.
            case PORT_ACT_MENU_UP:      queueEvent(EV_KEY, SDL_SCANCODE_UP, state); return;
            case PORT_ACT_MENU_DOWN:    queueEvent(EV_KEY, SDL_SCANCODE_DOWN, state); return;
            case PORT_ACT_MENU_LEFT:    queueEvent(EV_KEY, SDL_SCANCODE_LEFT, state); return;
            case PORT_ACT_MENU_RIGHT:   queueEvent(EV_KEY, SDL_SCANCODE_RIGHT, state); return;
            case PORT_ACT_MENU_SELECT:
            case PORT_ACT_MENU_CONFIRM: queueEvent(EV_KEY, SDL_SCANCODE_RETURN, state); return;
            case PORT_ACT_MENU_BACK:
            case PORT_ACT_MENU_ABORT:   queueEvent(EV_KEY, SDL_SCANCODE_ESCAPE, state); return;

            // The menu screen's explicit click button. Injected as a real SDL
            // mouse event, same path as the pointer itself.
            case PORT_ACT_MOUSE_LEFT:   MouseButton(state, BUTTON_PRIMARY); return;

            default: break;
        }
    }

    // Nothing below starts behind a menu. Releases still fall through, because
    // the touch layer emits its button-ups as the game controls fade out - by
    // which point the screen mode has already changed.
    if (menuUp && state)
        return;

    switch (action)
    {
        // Analog movement; the sticks use PortableMove* instead.
        case PORT_ACT_FWD:
        case PORT_ACT_BACK:
        case PORT_ACT_MOVE_RIGHT:
        case PORT_ACT_MOVE_LEFT:
        case PORT_ACT_RIGHT:
        case PORT_ACT_LEFT:
        {
            std::lock_guard<std::mutex> lock(inputMutex);
            if (action == PORT_ACT_FWD) s_forwardDown = state != 0;
            if (action == PORT_ACT_BACK) s_backDown = state != 0;
            if (action == PORT_ACT_MOVE_RIGHT) s_rightDown = state != 0;
            if (action == PORT_ACT_MOVE_LEFT) s_leftDown = state != 0;
            if (action == PORT_ACT_RIGHT) s_turnRightDown = state != 0;
            if (action == PORT_ACT_LEFT) s_turnLeftDown = state != 0;
            s_moveDigital = int(s_forwardDown) - int(s_backDown);
            s_strafeDigital = int(s_rightDown) - int(s_leftDown);
            return;
        }

        // Impulses resolve by number, so these survive any rebinding.
        case PORT_ACT_RELOAD:      if (state) queueEvent(EV_IMPULSE, 13, 0); return;
        case PORT_ACT_NEXT_WEP:    if (state) queueEvent(EV_IMPULSE, 14, 0); return;
        case PORT_ACT_PREV_WEP:    if (state) queueEvent(EV_IMPULSE, 15, 0); return;
        case PORT_ACT_FLASH_LIGHT: if (state) queueEvent(EV_IMPULSE, 50, 0); return;
        case PORT_ACT_HOLSTER_WEAPON: if (state) queueEvent(EV_IMPULSE, 51, 0); return;

        default:
            // Weapon number grid. PORT_ACT_WEAP0 is impulse 0 (blaster), so the
            // grid's 1..0 maps straight onto Quake 4's impulse 0..9.
            if (action >= PORT_ACT_WEAP0 && action <= PORT_ACT_WEAP10)
            {
                if (state)
                    queueEvent(EV_IMPULSE, action - PORT_ACT_WEAP0, 0);
                return;
            }
            break;
    }

    // Everything left is a held button, set on the usercmd by name instead of
    // as the key default.cfg happens to bind it to, so rebinding in-game cannot
    // break it. See docs/engines/quake4.md.
    switch (action)
    {
        case PORT_ACT_ATTACK:     queueButton(QUAKE4_BTN_ATTACK, 1, state); break;
        case PORT_ACT_ALT_ATTACK: queueButton(QUAKE4_BTN_ZOOM, 1, state); break;
        case PORT_ACT_ZOOM_IN:    queueButton(QUAKE4_BTN_ZOOM, 2, state); break;

        case PORT_ACT_JUMP:       queueButton(QUAKE4_BTN_MOVE_UP, 1, state); break;
        case PORT_ACT_UP:         queueButton(QUAKE4_BTN_MOVE_UP, 2, state); break;
        case PORT_ACT_CROUCH:     queueButton(QUAKE4_BTN_MOVE_DOWN, 1, state); break;
        case PORT_ACT_DOWN:       queueButton(QUAKE4_BTN_MOVE_DOWN, 2, state); break;
        case PORT_ACT_SPEED:      queueButton(QUAKE4_BTN_SPEED, 1, state); break;
        case PORT_ACT_SPRINT:     queueButton(QUAKE4_BTN_SPEED, 2, state); break;
        case PORT_ACT_STRAFE:     queueButton(QUAKE4_BTN_STRAFE, 1, state); break;
        case PORT_ACT_USE_WEAPON_WHEEL: queueButton(QUAKE4_BTN_WEAPON_WHEEL, 1, state); break;

        // Objectives is hold-to-show, not an impulse: PerformImpulse's IMPULSE_19
        // case is empty, HandleObjectiveInput watches BUTTON_SCORES instead.
        case PORT_ACT_HELPCOMP:   queueButton(QUAKE4_BTN_SCORES, 1, state); break;

        default: break;
    }
}

static float boundedLook(float accumulated, float delta)
{
    if (!std::isfinite(delta))
        return accumulated;
    const float value = accumulated + delta;
    return value > 32767.0f ? 32767.0f : value < -32767.0f ? -32767.0f : value;
}

void PortableMove(float fwd, float strafe)
{
    PortableMoveFwd(fwd);
    PortableMoveSide(strafe);
}

void PortableMoveFwd(float fwd)
{
    std::lock_guard<std::mutex> lock(inputMutex);
    s_moveStick = clampUnit(fwd);
}

void PortableMoveSide(float strafe)
{
    std::lock_guard<std::mutex> lock(inputMutex);
    s_strafeStick = clampUnit(strafe);
}

void PortableLookPitch(int mode, float pitch)
{
    std::lock_guard<std::mutex> lock(inputMutex);
    if (mode == LOOK_MODE_JOYSTICK)
        s_pitchJoy = clampUnit(pitch);
    else
        s_pitchMouse = boundedLook(s_pitchMouse, pitch * LOOK_MOUSE_PITCH_SCALE);
}

void PortableLookYaw(int mode, float yaw)
{
    std::lock_guard<std::mutex> lock(inputMutex);
    if (mode == LOOK_MODE_JOYSTICK)
        s_yawJoy = clampUnit(yaw);
    else
        s_yawMouse = boundedLook(s_yawMouse, yaw * LOOK_MOUSE_YAW_SCALE);
}

void PortableMouse(float dx, float dy)
{
    std::lock_guard<std::mutex> lock(inputMutex);
    s_yawMouse = boundedLook(s_yawMouse, dx * LOOK_MOUSE_YAW_SCALE);
    s_pitchMouse = boundedLook(s_pitchMouse, dy * LOOK_MOUSE_PITCH_SCALE);
}

void PortableMouseAbs(float x, float y)
{
}

void PortableMouseButton(int state, int button, float dx, float dy)
{
    queueEvent(EV_MOUSE_BUTTON, button, state);
}

void PortableCommand(const char *cmd)
{
    queueCommand(cmd);
}

void PortableAutomapControl(float zoom, float x, float y)
{
}

int PortableShowKeyboard(void)
{
    return Quake4_GetScreenMode() == 4;
}

bool PortableSetAlwaysRun(bool run)
{
    queueCommand(run ? "set in_alwaysRun 1" : "set in_alwaysRun 0");
    return run;
}

touchscreemode_t PortableGetScreenMode()
{
    return (touchscreemode_t) Quake4_GetScreenMode();
}
