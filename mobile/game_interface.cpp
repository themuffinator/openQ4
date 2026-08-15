// Quake 4's Portable* implementation. See docs/engines/quake4.md in Sigma Touch.
//
// Nothing here includes an engine header: everything that has to touch engine
// state goes through mobile/quake4_bridge.h, whose implementation lives in
// sys/android/android_sdl3.cpp and only ever runs on the engine thread.

#include <pthread.h>
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

// The engine prints through common->Printf, which already reaches logcat, but
// third-party code (SDL, OpenAL, the C runtime) still writes to stdout/stderr,
// where it would otherwise vanish.
static void *stdio_pump(void *arg)
{
    int fd = (int) (long) arg;
    char buf[512];
    ssize_t n;

    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0)
    {
        if (buf[n - 1] == '\n')
            n--;

        buf[n] = 0;
        Q4_LOGI("%s", buf);
    }

    return NULL;
}

static void redirect_stdio_to_logcat()
{
    int pipes[2];
    pthread_t thread;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (pipe(pipes) != 0)
        return;

    dup2(pipes[1], STDOUT_FILENO);
    dup2(pipes[1], STDERR_FILENO);

    if (pthread_create(&thread, NULL, stdio_pump, (void *) (long) pipes[0]) == 0)
        pthread_detach(thread);
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
// push here (or set one of the volatile axis levels); Quake4_DrainTouchInput,
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
    EV_COMMAND       // a = index into the command ring
};

#define EVENT_QUEUE_SIZE 128
#define COMMAND_RING_SIZE 8
#define COMMAND_MAX_LEN 128

struct TouchEvent
{
    int type;
    int a;
    int b;
};

static TouchEvent eventQueue[EVENT_QUEUE_SIZE];
static volatile int eventHead; // written by the touch thread
static volatile int eventTail; // written by the engine thread

static char commandRing[COMMAND_RING_SIZE][COMMAND_MAX_LEN];
static volatile int commandNext;

static void queueEvent(int type, int a, int b)
{
    int head = eventHead;
    int next = (head + 1) % EVENT_QUEUE_SIZE;

    if (next == eventTail) // full: drop rather than block the UI thread
        return;

    eventQueue[head].type = type;
    eventQueue[head].a = a;
    eventQueue[head].b = b;
    eventHead = next;
}

static void queueCommand(const char *cmd)
{
    if (!cmd || !cmd[0])
        return;

    int slot = commandNext;
    commandNext = (slot + 1) % COMMAND_RING_SIZE;

    strncpy(commandRing[slot], cmd, COMMAND_MAX_LEN - 1);
    commandRing[slot][COMMAND_MAX_LEN - 1] = 0;

    queueEvent(EV_COMMAND, slot, 0);
}

// Analog levels rather than events: only the latest value matters, so there is
// nothing to queue and nothing to lose if a frame is missed.
static volatile float s_moveStick, s_strafeStick;
static volatile int s_moveDigital, s_strafeDigital; // dpad, -1/0/+1
static volatile float s_yawJoy, s_pitchJoy;         // held look rate

// Accumulated look, drained per frame. Float, not int: the engine only takes
// whole mouse counts, but a touch event's own contribution is often a fraction
// of one - so truncating per event threw that fraction away every time and
// slow drags moved in visible steps. Keeping the remainder here makes the
// motion continuous.
static volatile float s_yawMouse, s_pitchMouse;

// A screen-fraction swipe is worth about this many mouse pixels. Matches the
// feel of the other OpenTouch ports at the default in-game sensitivity.
#define LOOK_MOUSE_YAW_SCALE   3000.0f
#define LOOK_MOUSE_PITCH_SCALE 2000.0f

// The touch layer's sticks come in pre-scaled well past unity (leftStick alone
// multiplies by 15 and 10), so the levels are clamped to -1..1 as they arrive
// and only turned into engine axis units here.
static float clampUnit(float v)
{
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
    while (eventTail != eventHead)
    {
        const TouchEvent &ev = eventQueue[eventTail];

        switch (ev.type)
        {
            case EV_KEY:          Quake4_PostKey(ev.a, ev.b); break;
            case EV_CHAR:         Quake4_PostChar(ev.a); break;
            case EV_MOUSE_DELTA:  Quake4_PostMouseDelta(ev.a, ev.b); break;
            case EV_MOUSE_BUTTON: Quake4_PostMouseButton(ev.a, ev.b); break;
            case EV_IMPULSE:      Quake4_TriggerImpulse(ev.a); break;
            case EV_COMMAND:      Quake4_PostCommand(commandRing[ev.a]); break;
        }

        eventTail = (eventTail + 1) % EVENT_QUEUE_SIZE;
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
                            s_strafeDigital ? s_strafeDigital * AXIS_MAX : axisValue(s_strafeStick));
    Quake4_PostJoystickAxis(QUAKE4_AXIS_PITCH,
                            s_moveDigital ? s_moveDigital * AXIS_MAX : axisValue(s_moveStick));

    // Look, only used when the player turns on joystick look mode; the default
    // is the mouse path below. JoystickMove does `viewangles[YAW] -= lookAxisX`
    // and `viewangles[PITCH] += lookAxisY`, and the touch layer has already
    // negated pitch relative to its mouse value, hence the signs.
    Quake4_PostJoystickAxis(QUAKE4_AXIS_SIDE, -axisValue(s_yawJoy));
    Quake4_PostJoystickAxis(QUAKE4_AXIS_FORWARD, axisValue(s_pitchJoy));

    // Take the whole counts and leave the remainder to be carried into the
    // next frame, so nothing is lost between drains.
    const int dx = (int)s_yawMouse;
    const int dy = (int)s_pitchMouse;
    if (dx || dy)
    {
        s_yawMouse -= (float)dx;
        s_pitchMouse -= (float)dy;
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
    switch (action)
    {
        // Menu and console navigation reads raw keys rather than going through
        // the rebindable path, so synthetic key presses are both correct and
        // simplest. The menus themselves are worked with the pointer (see
        // TouchInterface::mouseMove); these are what the gamepad and the yes/no
        // screen send.
        case PORT_ACT_MENU_UP:      queueEvent(EV_KEY, SDL_SCANCODE_UP, state); return;
        case PORT_ACT_MENU_DOWN:    queueEvent(EV_KEY, SDL_SCANCODE_DOWN, state); return;
        case PORT_ACT_MENU_LEFT:    queueEvent(EV_KEY, SDL_SCANCODE_LEFT, state); return;
        case PORT_ACT_MENU_RIGHT:   queueEvent(EV_KEY, SDL_SCANCODE_RIGHT, state); return;
        case PORT_ACT_MENU_SELECT:
        case PORT_ACT_MENU_CONFIRM: queueEvent(EV_KEY, SDL_SCANCODE_RETURN, state); return;
        case PORT_ACT_MENU_BACK:
        case PORT_ACT_MENU_ABORT:   queueEvent(EV_KEY, SDL_SCANCODE_ESCAPE, state); return;

        // The menu screen's explicit click button. Injected as a real SDL mouse
        // event, same path as the pointer itself.
        case PORT_ACT_MOUSE_LEFT:   MouseButton(state, BUTTON_PRIMARY); return;

        default: break;
    }

    switch (action)
    {
        // Analog movement; the sticks use PortableMove* instead.
        case PORT_ACT_FWD:        s_moveDigital = state ? 1 : 0; return;
        case PORT_ACT_BACK:       s_moveDigital = state ? -1 : 0; return;
        case PORT_ACT_MOVE_RIGHT: s_strafeDigital = state ? 1 : 0; return;
        case PORT_ACT_MOVE_LEFT:  s_strafeDigital = state ? -1 : 0; return;

        // Turn buttons have no analog source, so drive the look rate directly.
        case PORT_ACT_RIGHT: s_yawJoy = state ? 1.0f : 0.0f; return;
        case PORT_ACT_LEFT:  s_yawJoy = state ? -1.0f : 0.0f; return;

        // Impulses resolve by number, so these survive any rebinding.
        case PORT_ACT_RELOAD:      if (state) queueEvent(EV_IMPULSE, 13, 0); return;
        case PORT_ACT_NEXT_WEP:    if (state) queueEvent(EV_IMPULSE, 14, 0); return;
        case PORT_ACT_PREV_WEP:    if (state) queueEvent(EV_IMPULSE, 15, 0); return;
        case PORT_ACT_HELPCOMP:    if (state) queueEvent(EV_IMPULSE, 19, 0); return;
        case PORT_ACT_FLASH_LIGHT: if (state) queueEvent(EV_IMPULSE, 50, 0); return;
        case PORT_ACT_HOLSTER_WEAPON: if (state) queueEvent(EV_IMPULSE, 51, 0); return;

        case PORT_ACT_QUICKSAVE: if (state) queueCommand("savegame quick"); return;
        case PORT_ACT_QUICKLOAD: if (state) queueCommand("loadgame quick"); return;

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

    // Everything left is a held button, driven as its default bound key or
    // mouse button. That breaks the moment the player rebinds in-game; Phase 2
    // replaces it with a direct usercmd-button path. See docs/engines/quake4.md.
    switch (action)
    {
        // _attack and _zoom live on the mouse in default.cfg.
        case PORT_ACT_ATTACK:     queueEvent(EV_MOUSE_BUTTON, 1, state); break;
        case PORT_ACT_ALT_ATTACK:
        case PORT_ACT_ZOOM_IN:    queueEvent(EV_MOUSE_BUTTON, 2, state); break;

        case PORT_ACT_JUMP:
        case PORT_ACT_UP:         queueEvent(EV_KEY, SDL_SCANCODE_SPACE, state); break;
        case PORT_ACT_CROUCH:
        case PORT_ACT_DOWN:       queueEvent(EV_KEY, SDL_SCANCODE_C, state); break;
        case PORT_ACT_SPEED:
        case PORT_ACT_SPRINT:     queueEvent(EV_KEY, SDL_SCANCODE_LSHIFT, state); break;
        case PORT_ACT_STRAFE:     queueEvent(EV_KEY, SDL_SCANCODE_LALT, state); break;
        case PORT_ACT_CONSOLE:    queueEvent(EV_KEY, SDL_SCANCODE_GRAVE, state); break;
        case PORT_ACT_USE_WEAPON_WHEEL: queueEvent(EV_KEY, SDL_SCANCODE_E, state); break;

        default: break;
    }
}

void PortableMove(float fwd, float strafe)
{
    PortableMoveFwd(fwd);
    PortableMoveSide(strafe);
}

void PortableMoveFwd(float fwd)
{
    s_moveStick = clampUnit(fwd);
}

void PortableMoveSide(float strafe)
{
    s_strafeStick = clampUnit(strafe);
}

void PortableLookPitch(int mode, float pitch)
{
    if (mode == LOOK_MODE_JOYSTICK)
        s_pitchJoy = clampUnit(pitch);
    else
        s_pitchMouse += pitch * LOOK_MOUSE_PITCH_SCALE;
}

void PortableLookYaw(int mode, float yaw)
{
    if (mode == LOOK_MODE_JOYSTICK)
        s_yawJoy = clampUnit(yaw);
    else
        s_yawMouse += yaw * LOOK_MOUSE_YAW_SCALE;
}

void PortableMouse(float dx, float dy)
{
    s_yawMouse += dx * LOOK_MOUSE_YAW_SCALE;
    s_pitchMouse += dy * LOOK_MOUSE_PITCH_SCALE;
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
    return 0;
}

bool PortableSetAlwaysRun(bool run)
{
    return run;
}

touchscreemode_t PortableGetScreenMode()
{
    return (touchscreemode_t) Quake4_GetScreenMode();
}
