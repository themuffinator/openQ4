#!/usr/bin/env python3
"""Execute the production menu traversal/hover code against an isolated tree.

No window, input device, or running game is used. A C++ compiler is required.
"""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def extract(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for i in range(opening, len(source)):
        depth += (source[i] == '{') - (source[i] == '}')
        if depth == 0:
            return source[start:i + 1]
    raise AssertionError(f'Unterminated {signature}')


HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>
#include <cstdio>
template<class T> struct idList : std::vector<T> {
    using std::vector<T>::operator=;
    int Num() const { return static_cast<int>(this->size()); }
    void Append(T v) { this->push_back(v); }
    void SetNum(int n) { this->resize(static_cast<size_t>(n)); }
    int FindIndex(T v) const {
        auto i = std::find(this->begin(), this->end(), v);
        return i == this->end() ? -1 : static_cast<int>(i - this->begin());
    }
};
struct Text : std::string { void Clear() { clear(); } };
enum { WIN_CANFOCUS = 1, SE_MOUSE, SE_KEY, K_MOUSE1 = 187, K_MWHEELUP = 196,
       K_UPARROW = 133, K_DOWNARROW, K_LEFTARROW, K_RIGHTARROW, K_KP_UPARROW = 160, K_KP_DOWNARROW, K_KP_LEFTARROW,
       K_KP_RIGHTARROW, K_PGUP, K_PGDN, K_HOME, K_END, K_DEL, K_INS, K_BACKSPACE, K_ENTER };
struct sysEvent_t { int evType, evValue, evValue2; };
struct Gui {
    bool controller = false;
    Text pending;
    bool ControllerNavigation() const { return controller; }
    void SetControllerNavigation(bool v) { controller = v; }
    Text& GetPendingCmd() { return pending; }
};
struct idRectangle { float x = 0, y = 0, w = 0, h = 0; };
template<class T> T Min(T a, T b) { return a < b ? a : b; }
template<class T> T Max(T a, T b) { return a > b ? a : b; }
struct idWindow {
    idList<idWindow*> children;
    idRectangle drawRect;
    const idRectangle& GetDrawRect() const { return drawRect; }
    idWindow *focusedChild = nullptr, *overChild = nullptr, *capture = nullptr;
    Gui *gui;
    Text cmd;
    bool visible = true, noEvents = false, hover = false;
    int flags = 0, enters = 0, exits = 0, routes = 0;
    explicit idWindow(Gui& g, bool focusable = false) : gui(&g), flags(focusable ? WIN_CANFOCUS : 0) {}
    virtual ~idWindow() {}
    int GetChildCount() const { return children.Num(); }
    idWindow* GetChild(int i) { return children[i]; }
    bool IsVisible() const { return visible; }
    bool HasNoEvents() const { return noEvents; }
    int GetFlags() const { return flags; }
    idWindow* GetFocusedChild() { return focusedChild; }
    idWindow* GetCaptureChild() { return capture; }
    idWindow* SetFocus(idWindow* w, bool) { auto old = focusedChild; focusedChild = w; return old; }
    void MouseEnter() { ++enters; }
    void MouseExit() { ++exits; }
    void RouteMouseCoords(float, float) { ++routes; }
    void ClearMouseHover();
    void UpdateOwnership(bool controllerEvent, const sysEvent_t* event);
};
struct idChoiceWindow : idWindow { using idWindow::idWindow; };
struct idSliderWindow : idWindow { using idWindow::idWindow; bool vertical = false; bool IsVertical() const { return vertical; } };
struct idListWindow : idWindow { using idWindow::idWindow; };
struct idEditWindow : idWindow { using idWindow::idWindow; bool wrap = false; bool IsMultiLine() const { return wrap; } };
struct idBindWindow : idWindow { using idWindow::idWindow; };
'''

TESTS = r'''
int main() {
    Gui gui;
    idWindow root(gui), group(gui), a(gui,true), b(gui,true), c(gui,true), hidden(gui), h(gui,true), disabled(gui,true);
    root.children = {&group, &c, &hidden, &disabled};
    group.children = {&a, &b}; hidden.children = {&h};
    hidden.visible = false; disabled.noEvents = true;
    assert(NavigateFocus(&root, -1, true) && root.focusedChild == &c);
    assert(NavigateFocus(&root, -1, true) && root.focusedChild == &b);
    assert(NavigateFocus(&root, -1, true) && root.focusedChild == &a);
    assert(NavigateFocus(&root, -1, true) && root.focusedChild == &c);
    assert(NavigateFocus(&root, 1, true) && root.focusedChild == &a);
    assert(NavigateFocus(&root, 1, true) && root.focusedChild == &b);
    assert(NavigateFocus(&root, 1, true) && root.focusedChild == &c);
    root.focusedChild = &h;
    assert(NavigateFocus(&root, 1, true) && root.focusedChild == &a);
    root.focusedChild = &h;
    assert(NavigateFocus(&root, -1, true) && root.focusedChild == &c);
    assert(!NavigateFocus(&root, 0));
    idWindow empty(gui);
    assert(!NavigateFocus(&empty, 1));

    // Hover follows a nested mouse path, not the current focused window.
    root.overChild = &group; group.overChild = &b;
    group.hover = b.hover = true;
    const int groupExits = group.exits, bExits = b.exits;
    sysEvent_t controllerDown{SE_KEY, 0, 1};
    root.UpdateOwnership(true, &controllerDown);
    assert(gui.controller && !root.overChild && !group.overChild);
    assert(!group.hover && !b.hover);
    assert(group.exits == groupExits + 1 && b.exits == bExits + 1);
    int focusEnters = c.enters;
    root.UpdateOwnership(true, &controllerDown);
    assert(c.enters == focusEnters); // repeats don't re-enter old focus

    sysEvent_t idleMouse{SE_MOUSE, 0, 0};
    root.UpdateOwnership(false, &idleMouse);
    assert(gui.controller && root.routes == 0);
    sysEvent_t controllerUp{SE_KEY, 0, 0};
    root.UpdateOwnership(true, &controllerUp);
    assert(gui.controller);
    sysEvent_t motion{SE_MOUSE, 1, 0};
    root.UpdateOwnership(false, &motion);
    assert(!gui.controller && root.routes == 1);
    root.UpdateOwnership(true, &controllerDown);
    sysEvent_t click{SE_KEY, K_MOUSE1, 1};
    root.UpdateOwnership(false, &click);
    assert(!gui.controller && root.routes == 2);
    root.capture = &a;
    root.UpdateOwnership(true, &controllerDown);
    assert(!gui.controller); // do not interrupt an active mouse drag

    // The arrow keys ask a menu for the D-pad's focus steps, except while the
    // focused control uses that key itself or a control holds the capture.
    int direction = 0;
    sysEvent_t up{SE_KEY, K_UPARROW, 1}, down{SE_KEY, K_DOWNARROW, 1}, left{SE_KEY, K_KP_LEFTARROW, 1}, right{SE_KEY, K_RIGHTARROW, 1};
    sysEvent_t enter{SE_KEY, K_ENTER, 1}, moved{SE_MOUSE, 4, 0};
    assert(!KeyboardMenuFocusDirection(&root, &down, direction)); // a drag owns the keys
    root.capture = nullptr; root.focusedChild = &a;
    assert(KeyboardMenuFocusDirection(&root, &down, direction) && direction == 1);
    assert(KeyboardMenuFocusDirection(&root, &up, direction) && direction == -1);
    assert(KeyboardMenuFocusDirection(&root, &left, direction) && direction == -1);
    assert(KeyboardMenuFocusDirection(&root, &right, direction) && direction == 1);
    direction = 7;
    assert(!KeyboardMenuFocusDirection(&root, &enter, direction) && !KeyboardMenuFocusDirection(&root, &moved, direction));
    idChoiceWindow choice(gui, true); idSliderWindow slider(gui, true); idListWindow list(gui, true);
    idEditWindow edit(gui, true); idBindWindow bind(gui, true);
    root.focusedChild = &choice; // left and right change a choice; up and down leave it
    assert(!KeyboardMenuFocusDirection(&root, &left, direction) && !KeyboardMenuFocusDirection(&root, &right, direction));
    assert(KeyboardMenuFocusDirection(&root, &down, direction) && direction == 1);
    root.focusedChild = &slider;
    assert(!KeyboardMenuFocusDirection(&root, &right, direction) && KeyboardMenuFocusDirection(&root, &up, direction));
    root.focusedChild = &list; // up and down walk a list's rows
    assert(!KeyboardMenuFocusDirection(&root, &up, direction) && !KeyboardMenuFocusDirection(&root, &down, direction));
    assert(KeyboardMenuFocusDirection(&root, &right, direction) && direction == 1);
    root.focusedChild = &edit; // an edit field keeps its caret keys; a one-line field lets Up and Down step
    assert(!KeyboardMenuFocusDirection(&root, &left, direction) && !KeyboardMenuFocusDirection(&root, &right, direction));
    assert(KeyboardMenuFocusDirection(&root, &down, direction) && direction == 1);
    edit.wrap = true; // several lines scroll with Up and Down
    assert(!KeyboardMenuFocusDirection(&root, &down, direction) && !KeyboardMenuFocusDirection(&root, &up, direction));
    direction = 7;
    assert(!KeyboardMenuFocusDirection(&root, &left, direction) && direction == 7); // a refused key leaves the direction alone
    // A key row steps on: the GUI gives a row that waits for its key the next
    // key before the desktop sees it, so a row at rest must not hold the focus.
    root.focusedChild = &bind;
    assert(KeyboardMenuFocusDirection(&root, &down, direction) && direction == 1);
    assert(KeyboardMenuFocusDirection(&root, &up, direction) && direction == -1);
    {
        // Focus order: columns from the leading edge, each in reading order,
        // whatever order the GUI lists its windows in; a page's scroll bar is
        // no step.
        Gui page; idWindow desk(page);
        auto place = [](idWindow& w, float x, float y, float width, float height) { w.drawRect.x = x; w.drawRect.y = y; w.drawRect.w = width; w.drawRect.h = height; };
        idWindow tier0(page, true), tier1(page, true), match0(page, true), match1(page, true), start(page, true), back(page, true);
        idWindow value(page, true), button(page, true);
        idSliderWindow bar(page, true); bar.vertical = true;
        place(tier0, 18, 76, 162, 36); place(tier1, 18, 120, 162, 36);
        place(match0, 198, 76, 190, 34); place(match1, 198, 116, 190, 34);
        place(start, 400, 367, 230, 36); place(back, 536, 444, 105, 23);
        place(value, 432, 106, 88, 18); place(button, 526, 104, 79, 20); place(bar, 622, 104, 16, 253);
        for (idWindow* w : {&back, &match1, &button, &tier1, &start, &value, &match0, &tier0, static_cast<idWindow*>(&bar)}) desk.children.Append(w);
        idList<idWindow*> order;
        CollectFocusWindows(&desk, order);
        const idList<idWindow*> expected = [&] { idList<idWindow*> list; for (idWindow* w : {&tier0, &tier1, &match0, &match1, &value, &button, &start, &back}) list.Append(w); return list; }();
        assert(order == expected);
    }
    std::puts("menu_controller_navigation: behavioral checks passed");
}
'''


def main() -> None:
    source = (ROOT / 'src/ui/Window.cpp').read_text(encoding='utf-8')
    snippets = '\n'.join(extract(source, sig) for sig in (
        'static void SortFocusRows(', 'static void SortFocusOrder(', 'static void CollectFocusWindows(', 'static bool NavigateFocus(',
        'static bool WindowConsumesDirectionalNavigation(', 'static bool KeyboardMenuFocusDirection(',
        'void idWindow::ClearMouseHover()'))
    # Only a menu's desktop turns arrow keys into steps: a nested window, an
    # in-world GUI or a HUD never does.
    assert ('( flags & WIN_DESKTOP ) && ( flags & WIN_MENUGUI ) && '
            'KeyboardMenuFocusDirection( this, event, controllerFocusDirection )') in source
    ownership = extract(source, 'if ( controllerEvent && event->evValue2')
    next_start = source.index(ownership) + len(ownership)
    ownership += ' else ' + extract(source[next_start:], 'if ( gui->ControllerNavigation() )')
    # Keep dispatch/cursor suppression tied to the code under test.
    assert 'event->evType == SE_MOUSE && !gui->ControllerNavigation()' in source
    ui = (ROOT / 'src/ui/UserInterface.cpp').read_text(encoding='utf-8')
    assert 'if ( controllerNavigation )' in extract(ui, 'void idUserInterfaceLocal::DrawCursor()')
    handle = extract(ui, 'const char *idUserInterfaceLocal::HandleEvent(')
    assert handle.index('bindHandler->HandleEvent') < handle.index('desktop->HandleEvent')
    build = ROOT / '.tmp/menu-controller-navigation'
    build.mkdir(parents=True, exist_ok=True)
    cpp = build / 'test.cpp'
    cpp.write_text(HARNESS + snippets + '\nvoid idWindow::UpdateOwnership(bool controllerEvent, const sysEvent_t* event) {\n' + ownership + '\n}\n' + TESTS, encoding='utf-8')
    compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++') or shutil.which('cl')
    if not compiler:
        raise RuntimeError('Set CXX to a C++ compiler or run from a developer shell.')
    exe = build / ('test.exe' if os.name == 'nt' else 'test')
    args = [compiler, '/nologo', '/EHsc', '/std:c++17', str(cpp), f'/Fe:{exe}', f'/Fo:{build}/'] if Path(compiler).stem.lower() == 'cl' else [compiler, '-std=c++17', str(cpp), '-o', str(exe)]
    subprocess.run(args, check=True, cwd=build)
    subprocess.run([str(exe)], check=True, cwd=build)


if __name__ == '__main__':
    main()
