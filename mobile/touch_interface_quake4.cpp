//
// Quake 4's touch control layout, adapted from the sibling :AVP module's.
// AvP's three species screens collapse to one game screen here, and the
// species abilities are replaced by Quake 4's flashlight, reload, zoom and
// objectives.
//

#include "touch_interface.h"
#include "quake4_bridge.h"
#include "SDL3/SDL_keycode.h"
#include "SDL3/SDL_scancode.h"

// Device screen size in pixels, owned by the shared JNI glue.
extern int mobile_screen_width;
extern int mobile_screen_height;

void TouchInterface::openGLStart()
{
    touchcontrols::gl_startRender();
}

void TouchInterface::openGLEnd()
{
    touchcontrols::gl_endRender();
}

//
// Menu pointer: relative drag of the engine's own cursor, tap to click. Quake 4's
// menus are mouse-driven GUIs (idUserInterface), so this is the only sensible way
// to work them - the same shape :OpenJK uses in Psi Touch.
//
// Goes in as real SDL mouse events rather than through the quake4_bridge ring
// buffer: SDL's event queue is thread-safe by design, and injecting there means
// the engine's existing menu-mouse handling applies unchanged.
//
void TouchInterface::mouseMove(int action, float x, float y, float mouse_x, float mouse_y)
{
    // Leave the top row alone, that is where the menu screen's buttons are.
    if (y < (2.0f / 16.0f))
        return;

    if (action == TOUCHMOUSE_MOVE)
    {
        MouseMove(mouse_x * mobile_screen_width, mouse_y * mobile_screen_height);
    }
    else if (action == TOUCHMOUSE_TAP)
    {
        // Hold for a few frames: the GUI samples button state per frame, so a
        // press and release inside one frame reads as never-clicked.
        MouseButton(1, BUTTON_PRIMARY);
        waitFrames(3);
        MouseButton(0, BUTTON_PRIMARY);
    }
}

void TouchInterface::addGameControls(touchcontrols::TouchControls *tc)
{
    tc->setAlpha(touchSettings.alpha);
    tc->addControl(new touchcontrols::Button("back", touchcontrols::RectF(0, 0, 2, 2), "back_button", KEY_BACK_BUTTON, false, false, "Show menu"));
    tc->addControl(new touchcontrols::Button("attack", touchcontrols::RectF(20, 7, 23, 10), "shoot", KEY_SHOOT, false, false, "Attack!"));
    tc->addControl(new touchcontrols::Button("attack2", touchcontrols::RectF(3, 5, 6, 8), "shoot", KEY_SHOOT, false, true, "Attack! (duplicate)"));

    tc->addControl(new touchcontrols::Button("zoom", touchcontrols::RectF(23, 6, 26, 9), "binocular", PORT_ACT_ZOOM_IN, false, false, "Zoom"));
    tc->addControl(new touchcontrols::Button("reload", touchcontrols::RectF(21, 5, 23, 7), "reload", PORT_ACT_RELOAD, false, false, "Reload"));
    tc->addControl(new touchcontrols::Button("flashlight", touchcontrols::RectF(16, 3, 18, 5), "flashlight", PORT_ACT_FLASH_LIGHT, false, false, "Flashlight"));
    tc->addControl(new touchcontrols::Button("objectives", touchcontrols::RectF(18, 3, 20, 5), "map", PORT_ACT_HELPCOMP, false, false, "Objectives"));

    tc->addControl(new touchcontrols::Button("quick_save", touchcontrols::RectF(24, 0, 26, 2), "save", PORT_ACT_QUICKSAVE, false, false, "Quick save"));
    tc->addControl(new touchcontrols::Button("quick_load", touchcontrols::RectF(20, 0, 22, 2), "load", PORT_ACT_QUICKLOAD, false, false, "Quick load"));

    tc->addControl(new touchcontrols::Button("keyboard", touchcontrols::RectF(8, 0, 10, 2), "keyboard", KEY_SHOW_KBRD, false, false, "Show keyboard"));
    tc->addControl(new touchcontrols::Button("show_mouse", touchcontrols::RectF(4, 0, 6, 2), "left_mouse", KEY_USE_MOUSE, false, true, "Use mouse"));
    tc->addControl(new touchcontrols::Button("console", touchcontrols::RectF(6, 0, 8, 2), "tild", PORT_ACT_CONSOLE, false, true, "Console"));

    tc->addControl(new touchcontrols::Button("jump", touchcontrols::RectF(24, 3, 26, 5), "jump", PORT_ACT_JUMP, false, false, "Jump"));
    tc->addControl(new touchcontrols::Button("crouch_toggle", touchcontrols::RectF(24, 14, 26, 16), "crouch", PORT_ACT_CROUCH, false, false, "Crouch"));

    tc->addControl(new touchcontrols::Button("show_custom", touchcontrols::RectF(0, 7, 2, 9), "custom_show", KEY_SHOW_CUSTOM, false, true, "Show custom"));
    tc->addControl(new touchcontrols::Button("show_weapons", touchcontrols::RectF(12, 14, 14, 16), "show_weapons", KEY_SHOW_WEAPONS, false, false, "Show numbers"));
    tc->addControl(new touchcontrols::Button("next_weapon", touchcontrols::RectF(0, 3, 3, 5), "next_weap", PORT_ACT_NEXT_WEP, false, false, "Next weapon"));
    tc->addControl(new touchcontrols::Button("prev_weapon", touchcontrols::RectF(0, 5, 3, 7), "prev_weap", PORT_ACT_PREV_WEP, false, false, "Prev weapon"));

    touchcontrols::ButtonGrid *dpad = new touchcontrols::ButtonGrid("dpad_move", touchcontrols::RectF(6, 3, 12, 7), "", 3, 2, true, "Movement btns (WASD)");

    dpad->addCell(0, 1, "direction_left", PORT_ACT_MOVE_LEFT);
    dpad->addCell(2, 1, "direction_right", PORT_ACT_MOVE_RIGHT);
    dpad->addCell(1, 0, "direction_up", PORT_ACT_FWD);
    dpad->addCell(1, 1, "direction_down", PORT_ACT_BACK);
    tc->addControl(dpad);

    touchcontrols::TouchJoy *right = new touchcontrols::TouchJoy("touch", touchcontrols::RectF(17, 4, 26, 16), "look_arrow", "fixed_stick_circle");
    tc->addControl(right);
    right->signal_move.connect(sigc::mem_fun(this, &TouchInterface::rightStick));
    right->signal_double_tap.connect(sigc::mem_fun(this, &TouchInterface::rightDoubleTap));

    touchcontrols::TouchJoy *left = new touchcontrols::TouchJoy("stick", touchcontrols::RectF(0, 7, 8, 16), "strafe_arrow", "fixed_stick_circle");
    tc->addControl(left);
    left->signal_move.connect(sigc::mem_fun(this, &TouchInterface::leftStick));
    left->signal_double_tap.connect(sigc::mem_fun(this, &TouchInterface::leftDoubleTap));

    // SWAPFIX
    left->registerTouchJoySWAPFIX(right);
    right->registerTouchJoySWAPFIX(left);

    touchJoyLeft = left;
    touchJoyRight = right;

    tc->signal_button.connect(sigc::mem_fun(this, &TouchInterface::gameButton));
    tc->signal_settingsButton.connect(sigc::mem_fun(this, &TouchInterface::gameSettingsButton));
}

void TouchInterface::createControls(std::string filesPath)
{
    tcMenuMain = new touchcontrols::TouchControls("menu", false, true, 10, false);
    tcYesNo = new touchcontrols::TouchControls("yes_no", false, false);
    tcGameMain = new touchcontrols::TouchControls("game", false, true, 1, true);
    tcGameWeapons = new touchcontrols::TouchControls("weapons", false, true, 1, false);
    tcWeaponWheel = new touchcontrols::TouchControls("weapon_wheel", false, true, 1, false);
    tcBlank = new touchcontrols::TouchControls("blank", true, false);
    tcCustomButtons = new touchcontrols::TouchControls("custom_buttons", false, true, 1, true);
    tcKeyboard = new touchcontrols::TouchControls("keyboard", false, false);
    tcGamepadUtility = new touchcontrols::TouchControls("gamepad_utility", false, false);
    tcMouse = new touchcontrols::TouchControls("mouse", false, false);
    // Hide the cog: with a gamepad and the weapon wheel enabled it would show.
    tcWeaponWheel->hideEditButton = true;

    //Menu -------------------------------------------
    //------------------------------------------------------
    // Mouse-driven, like :OpenJK - no arrow/enter buttons, just the utility row
    // along the top and a full-screen pointer under it.
    tcMenuMain->addControl(new touchcontrols::Button("back", touchcontrols::RectF(0, 0, 2, 2), "back_button", KEY_BACK_BUTTON));
    tcMenuMain->addControl(new touchcontrols::Button("keyboard", touchcontrols::RectF(2, 0, 4, 2), "keyboard", KEY_SHOW_KBRD));
    tcMenuMain->addControl(new touchcontrols::Button("console", touchcontrols::RectF(6, 0, 8, 2), "tild", PORT_ACT_CONSOLE));
    tcMenuMain->addControl(new touchcontrols::Button("show_custom", touchcontrols::RectF(9, 0, 11, 2), "custom_show", KEY_SHOW_CUSTOM));

    tcMenuMain->addControl(new touchcontrols::Button("gamepad", touchcontrols::RectF(22, 0, 24, 2), "gamepad", KEY_SHOW_GAMEPAD));
    tcMenuMain->addControl(new touchcontrols::Button("gyro", touchcontrols::RectF(24, 0, 26, 2), "gyro", KEY_SHOW_GYRO));
    tcMenuMain->addControl(new touchcontrols::Button("load_save_touch", touchcontrols::RectF(20, 0, 22, 2), "touchscreen_save", KEY_LOAD_SAVE_CONTROLS));

    // Explicit click, for anything the tap-to-click below can't reach.
    touchcontrols::Button *menuLeftMouse =
            new touchcontrols::Button("left_mouse", touchcontrols::RectF(23, 4, 26, 7), "left_mouse", PORT_ACT_MOUSE_LEFT);
    menuLeftMouse->setAllowPassThrough(false);
    tcMenuMain->addControl(menuLeftMouse);

    // Added last so every button above wins the touch.
    touchcontrols::Mouse *menuMouse = new touchcontrols::Mouse("mouse", touchcontrols::RectF(0, 0, 26, 16), "");
    menuMouse->setHideGraphics(true);
    menuMouse->setEditable(false);
    tcMenuMain->addControl(menuMouse);
    menuMouse->signal_action.connect(sigc::mem_fun(this, &TouchInterface::mouseMove));

    tcMenuMain->signal_button.connect(sigc::mem_fun(this, &TouchInterface::menuButton));
    tcMenuMain->setAlpha(0.8);
    tcMenuMain->setFixAspect(true);

    //Game -------------------------------------------
    //------------------------------------------------------
    addGameControls(tcGameMain);

    //Weapons -------------------------------------------
    //------------------------------------------------------
    // Quake 4's slots are impulse 0..10; the grid's 1..0 keys map onto
    // PORT_ACT_WEAP0..WEAP9 in game_interface.cpp.
    tcGameWeapons->addControl(new touchcontrols::Button("weapon1", touchcontrols::RectF(1, 14, 3, 16), "key_1", 1));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon2", touchcontrols::RectF(3, 14, 5, 16), "key_2", 2));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon3", touchcontrols::RectF(5, 14, 7, 16), "key_3", 3));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon4", touchcontrols::RectF(7, 14, 9, 16), "key_4", 4));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon5", touchcontrols::RectF(9, 14, 11, 16), "key_5", 5));

    tcGameWeapons->addControl(new touchcontrols::Button("weapon6", touchcontrols::RectF(15, 14, 17, 16), "key_6", 6));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon7", touchcontrols::RectF(17, 14, 19, 16), "key_7", 7));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon8", touchcontrols::RectF(19, 14, 21, 16), "key_8", 8));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon9", touchcontrols::RectF(21, 14, 23, 16), "key_9", 9));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon0", touchcontrols::RectF(23, 14, 25, 16), "key_0", 0));

    tcGameWeapons->signal_button.connect(sigc::mem_fun(this, &TouchInterface::selectWeaponButton));
    tcGameWeapons->setAlpha(0.8);

    //Weapon wheel -------------------------------------------
    //------------------------------------------------------
    wheelSelect = new touchcontrols::WheelSelect("weapon_wheel", touchcontrols::RectF(7, 2, 19, 14), "weapon_wheel_%d", wheelNbr);
    wheelSelect->signal_selected.connect(sigc::mem_fun(this, &TouchInterface::weaponWheel));
    wheelSelect->signal_enabled.connect(sigc::mem_fun(this, &TouchInterface::weaponWheelSelected));
    tcWeaponWheel->addControl(wheelSelect);

    if (touchSettings.weaponWheelOpaque)
        tcWeaponWheel->setAlpha(0.8);
    else
        tcWeaponWheel->setAlpha(touchSettings.alpha);

    //Blank -------------------------------------------
    //------------------------------------------------------
    tcBlank->addControl(new touchcontrols::Button("enter", touchcontrols::RectF(0, 0, 26, 16), "", 0x123));
    tcBlank->signal_button.connect(sigc::mem_fun(this, &TouchInterface::blankButton));

    //Keyboard -------------------------------------------
    //------------------------------------------------------
    uiKeyboard = new touchcontrols::UI_Keyboard("keyboard", touchcontrols::RectF(0, 8, 26, 16), "font_dual", 0, 0, 0);
    uiKeyboard->signal.connect(sigc::mem_fun(this, &TouchInterface::keyboardKeyPressed));
    tcKeyboard->addControl(uiKeyboard);
    // Pass touches through only where there is no keyboard
    tcKeyboard->setPassThroughTouch(touchcontrols::TouchControls::PassThrough::NO_CONTROL);

    //Yes No -------------------------------------------
    //------------------------------------------------------
    tcYesNo->addControl(new touchcontrols::Button("yes", touchcontrols::RectF(8, 12, 11, 15), "key_y", PORT_ACT_MENU_CONFIRM));
    tcYesNo->addControl(new touchcontrols::Button("no", touchcontrols::RectF(15, 12, 18, 15), "key_n", PORT_ACT_MENU_ABORT));
    tcYesNo->signal_button.connect(sigc::mem_fun(this, &TouchInterface::menuButton));
    tcYesNo->setAlpha(0.8);

    //Gamepad utility -------------------------------------------
    //------------------------------------------------------
    touchcontrols::ButtonGrid *gamepadUtils = new touchcontrols::ButtonGrid("gamepad_grid", touchcontrols::RectF(8, 5, 18, 11), "gamepad_utils_bg", 3, 2);

    gamepadUtils->addCell(0, 0, "back_button", KEY_BACK_BUTTON);
    gamepadUtils->addCell(1, 0, "keyboard", KEY_SHOW_KBRD);
    gamepadUtils->addCell(1, 1, "tild", PORT_ACT_CONSOLE);
    gamepadUtils->addCell(2, 0, "save", PORT_ACT_QUICKSAVE);
    gamepadUtils->addCell(2, 1, "load", PORT_ACT_QUICKLOAD);

    gamepadUtils->signal_outside.connect(sigc::mem_fun(this, &TouchInterface::gameUtilitiesOutside));

    tcGamepadUtility->addControl(gamepadUtils);
    tcGamepadUtility->setAlpha(0.9);
    tcGamepadUtility->signal_button.connect(sigc::mem_fun(this, &TouchInterface::gameUtilitiesButton));

    // Relative-drag mouse look (menus / touch pointer)
    touchcontrols::Mouse *mouse = new touchcontrols::Mouse("mouse", touchcontrols::RectF(0, 0, 26, 16), "");
    mouse->setHideGraphics(true);
    mouse->setEditable(false);
    tcMouse->addControl(mouse);
    mouse->signal_action.connect(sigc::mem_fun(this, &TouchInterface::mouseMove));
    tcMouse->addControl(new touchcontrols::Button("back", touchcontrols::RectF(0, 0, 2, 2), "back_button", KEY_BACK_BUTTON, false, false, "Back"));
    tcMouse->addControl(new touchcontrols::Button("left_button", touchcontrols::RectF(0, 6, 3, 10), "left_mouse", KEY_LEFT_MOUSE, false, false, "Left click"));
    tcMouse->signal_button.connect(sigc::mem_fun(this, &TouchInterface::mouseButton));

    std::string newSettings = (std::string) filesPath + "/touch_settings_" ENGINE_NAME ".xml";

    touchcontrols::tTouchSettingsModifier modifier;
    modifier.mouseLookVisible = true;

    UI_tc = touchcontrols::createDefaultSettingsUI(&controlsContainer, newSettings, &modifier);
    UI_tc->setAlpha(1);

    //---------------------------------------------------------------
    //---------------------------------------------------------------
    controlsContainer.addControlGroup(tcKeyboard);
    controlsContainer.addControlGroup(tcGamepadUtility); // before gamemain so touches don't go through
    controlsContainer.addControlGroup(tcCustomButtons);
    controlsContainer.addControlGroup(tcGameMain);
    controlsContainer.addControlGroup(tcYesNo);
    controlsContainer.addControlGroup(tcGameWeapons);
    controlsContainer.addControlGroup(tcMenuMain);
    controlsContainer.addControlGroup(tcWeaponWheel);
    controlsContainer.addControlGroup(tcBlank);
    controlsContainer.addControlGroup(tcMouse);

    tcMenuMain->setXMLFile((std::string) filesPath + "/menu.xml");
    tcGameMain->setXMLFile((std::string) filesPath + "/game_" ENGINE_NAME ".xml");
    tcWeaponWheel->setXMLFile((std::string) filesPath + "/weaponwheel_" ENGINE_NAME ".xml");
    tcGameWeapons->setXMLFile((std::string) filesPath + "/weapons_" ENGINE_NAME ".xml");
    tcCustomButtons->setXMLFile((std::string) filesPath + "/custom_buttons_0_" ENGINE_NAME ".xml");
}

void TouchInterface::blankButton(int state, int code)
{
    PortableKeyEvent(state, SDL_SCANCODE_SPACE, 0);
}

void TouchInterface::automapButton(int state, int code)
{
}

void TouchInterface::newFrame()
{
    touchscreemode_t screenMode = PortableGetScreenMode();

    // Hack to show custom buttons while in the menu to bind keys
    if (screenMode == TS_MENU && showCustomMenu == true)
    {
        screenMode = TS_CUSTOM;
    }

    // TS_CONSOLE has no controls of its own, which would leave nothing on screen
    // but the soft keyboard. Reuse the menu overlay: its top row can toggle the
    // console back off and re-show the keyboard, and the console is mouse-driven.
    if (screenMode == TS_CONSOLE)
    {
        screenMode = TS_MENU;
    }

    updateTouchScreenModeOut(screenMode);
    updateTouchScreenModeIn(screenMode);

    currentScreenMode = screenMode;
}

void TouchInterface::newGLContext()
{
}
