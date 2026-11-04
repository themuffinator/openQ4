// Android/SigmaTouch support by emileb: https://github.com/emileb/openQ4/tree/android
// Integrated and adapted for current openQ4; licensed under GPL-3.0-or-later.

#ifndef touch_interface_h
#define touch_interface_h

// The shared Clibs sources include this header, and NO_SEC is what keeps them
// off Delta Touch's secure/license tree, which is not part of this app.
#define NO_SEC

#include "touch_interface_base.h"

class TouchInterface : public TouchInterfaceBase
{
public:
    void createControls(std::string filesPath);

    void openGLEnd();

    void openGLStart();

    void blankButton(int state, int code);

    void newFrame();

    void automapButton(int state, int code);

    void newGLContext();

    void mouseMove(int action, float x, float y, float mouse_x, float mouse_y);

private:
    void addGameControls(touchcontrols::TouchControls *tc);
    void updateControlLabels();
    bool labelsLocalized = false;
};

#endif /* touch_interface_h */
