#ifndef touch_interface_h
#define touch_interface_h

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
};

#endif /* touch_interface_h */
