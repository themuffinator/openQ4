"""CONTROLLER TEST (docs/dev/roadmap.md PAD-07): the Controls page's panel
that shows what the controller in use is doing, a pop-up the CONTROLLER tab's
Test Controller row opens.

The first line names the controller, how it is powered and, where it reports
one, its battery. Both sticks follow: each a ring for the stick's full
throw, the dead zone filled at its centre and the outer limit drawn inside the
rim, both at the sizes the AIM tab's settings give (the look stick's on the
right, or on the left with Southpaw), and a dot where the device says the
stick is, before any dead zone. Beside them each trigger's travel fills a bar
with a mark at Trigger Press, the gyro's dot moves with the turn it asks for
beside its calibration state, and a line names the motor the rumble test is
running. A row of the controller's buttons, drawn as the active family's
glyphs, lights each one held. Where Steam Input stands between the game and
the controller and passes no gyro or touchpad on, a line says so and names
the Steam Deck guide.

TEST RUMBLE runs each motor the controller has, one after another
(controlsRumbleTest), and has the focus as the panel opens, so the Accept
button is seen lit and felt at once; CLOSE, or Back, closes the panel, so the
Back button is the one input the panel cannot show held. The session publishes
controllerTest.* every frame the page shows
(idSessionLocal::UpdateControllerTestPanel)."""
from __future__ import annotations

from build_retained_screens import (ORANGE, U, Document, absolute, circle_path, colour, confirmation, group, keyword, label,
                                    length, number, path, rgb, solid, stroke, transform, typeface, vector)
from retained_key_glyphs import pad_slot
from retained_mp_menus import PLACEHOLDER

IDENT = "controllerTestModal"
SHOW = f"{IDENT}Show"
ROW_LABEL, TITLE, RUMBLE, CLOSE = "#str_232753", "#str_232754", "#str_232755", "#str_200923"
MOTORS = ("#str_232756", "#str_232757", "#str_232758", "#str_232759")
POWER = ("#str_232760", "#str_232761", "#str_232762", "#str_232763")     # battery, charging, charged, wired
NO_CONTROLLER = "#str_232764"
CALIBRATION = ("#str_232765", "#str_232766", "#str_232767")              # not calibrated, calibrating, calibrated
STEAM = "#str_232768"
BUTTONS = 22                  # K_JOY1..K_JOY22 (Session_menu.cpp CONTROLLER_TEST_BUTTONS)
# The buttons' row, by key number: bumpers, face, menu and view, the D-pad,
# home, touchpad and the paddles. The stick presses (13, 14) stand under
# their sticks and the triggers (16 left, 15 right) beside their bars.
STRIP = (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 17, 18, 19, 20, 21, 22)
WIDTH = 620.0                 # dp: the dialog
PAD = 33.0                    # dp: the contents' inset, a dialog body's
STICK, THROW = 140.0, 64.0    # dp: a stick's box, and its full throw's radius
SIDE = 350.0                  # dp: where the triggers, gyro and motor line start
BAR = 195.0                   # dp: a trigger's travel
GLYPH = 22.0                  # dp: a button glyph's height
DIM, LIT = 0.3, 1.0           # a button's glyph, released and held
WHITE = [1, 1, 1, 0.9]


def st(name: str) -> dict:
    return {"state": f"controllerTest.{name}"}


def op(name: str, *args) -> dict:
    return {"op": name, "args": list(args)}


def choose(state: dict, keys: tuple, first: int = 1) -> dict:
    """The key whose number `state` holds, counting from `first`; none otherwise."""
    result = ""
    for index in reversed(range(len(keys))):
        result = op("select", op("==", state, first + index), keys[index], result)
    return result


def glyph(doc: Document, key: int) -> dict:
    """One button in the active family's art, dim until it is held."""
    name = f"controllerTest.key{key}"
    slot = pad_slot(doc, f"test-button-{key}", name)
    slot["properties"].update({"display": keyword("flex"), "opacity": number(DIM)})
    doc.state[f"controllerTest.b{key}"] = {"type": "boolean", "initial": False}
    doc.bind(f"test-button-{key}.opacity", f"test-button-{key}", "opacity", op("select", st(f"b{key}"), LIT, DIM))
    return slot


def glyphs(ident: str, left: float, top: float, width: float, slots: list, centre: bool = False) -> dict:
    """A row of glyphs: the strip's font size is a glyph's height."""
    return group(ident, {**absolute(left=left, top=top, width=width, height=GLYPH), "display": keyword("flex"),
                         "flex-direction": keyword("row"), "align-items": keyword("center"),
                         "justify-content": keyword("center" if centre else "flex-start"), "font-size": length(GLYPH),
                         "pointer-events": keyword("none")}, slots)


def stick(doc: Document, ident: str, left: float, top: float, x: str, y: str, dead: dict, outer: dict, press: int) -> list:
    """A stick: its throw, dead zone, outer limit and where it is, with its
    press under it."""
    mid = STICK / 2
    whole = absolute(left=0, top=0, width=STICK, height=STICK)
    line = lambda alpha, width=1.5: stroke(solid([1, 1, 1, alpha]), width, minimum=1)
    rim = vector(f"{ident}-rim", {**whole, "pointer-events": keyword("none")}, [
        circle_path("throw", mid, mid, THROW, fill=solid([0, 0, 0, 0.35]), line=line(0.45)),
        path("across", [(mid - THROW, mid), (mid + THROW, mid)], closed=False, stroke=line(0.12, 1)),
        path("down", [(mid, mid - THROW), (mid, mid + THROW)], closed=False, stroke=line(0.12, 1))])
    # The two zones are the throw's circle, scaled about the box's middle.
    limit = vector(f"{ident}-outer", {**whole, "transform": transform(), "pointer-events": keyword("none")},
                   [circle_path("limit", mid, mid, THROW, line=stroke(solid(rgb(ORANGE, 0.75)), 1.5, minimum=1))])
    doc.bind(f"{ident}-outer.transform", f"{ident}-outer", "transform", [0, 0, outer, outer, 0])
    zone = vector(f"{ident}-dead", {**whole, "transform": transform(), "pointer-events": keyword("none")},
                  [circle_path("zone", mid, mid, THROW, fill=solid([1, 1, 1, 0.16]), line=line(0.3, 1))])
    doc.bind(f"{ident}-dead.transform", f"{ident}-dead", "transform", [0, 0, dead, dead, 0])
    dot = vector(f"{ident}-dot", {**whole, "transform": transform(), "pointer-events": keyword("none")},
                 [circle_path("at", mid, mid, 6, fill=solid(rgb(ORANGE)), line=stroke(solid([0, 0, 0, 0.8]), 1, minimum=1))])
    along = lambda name: op("*", op("clamp", st(name), -1, 1), THROW)
    doc.bind(f"{ident}-dot.transform", f"{ident}-dot", "transform", [along(x), along(y), 1, 1, 0])
    box = group(ident, {**absolute(left=left, top=top, width=STICK, height=STICK), "pointer-events": keyword("none")},
                [rim, limit, zone, dot])
    return [box, glyphs(f"{ident}-press", left, top + STICK + 4, STICK, [glyph(doc, press)], centre=True)]


def trigger(doc: Document, ident: str, top: float, value: str, key: int, threshold: dict) -> list:
    """A trigger: its glyph, its travel as a bar and Trigger Press's mark."""
    left, height = SIDE + 42, 12.0
    frame = vector(f"{ident}-frame", {**absolute(left=left, top=top + 5, width=BAR, height=height), "pointer-events": keyword("none")},
                   [path("box", [(0, 0), (BAR, 0), (BAR, height), (0, height)], fill=solid([0, 0, 0, 0.35]),
                         stroke=stroke(solid([1, 1, 1, 0.45]), 1.5, minimum=1))])
    fill = group(f"{ident}-fill", {**absolute(left=left, top=top + 5, width=0, height=height),
                                   "background-color": colour(rgb(ORANGE)), "pointer-events": keyword("none")})
    doc.bind(f"{ident}-fill.width", f"{ident}-fill", "width", op("*", op("clamp", st(value), 0, 1), BAR))
    mark = group(f"{ident}-mark", {**absolute(left=left, top=top + 1, width=2, height=height + 8),
                                   "background-color": colour(WHITE), "pointer-events": keyword("none")})
    doc.bind(f"{ident}-mark.left", f"{ident}-mark", "left", op("+", left - 1, op("*", op("clamp", threshold, 0, 1), BAR)))
    return [glyphs(f"{ident}-glyph", SIDE, top, 40, [glyph(doc, key)]), frame, fill, mark]


def controller_test_modal(doc: Document) -> dict:
    """The panel; the page's device rows have declared the settings it reads."""
    doc.state.update({
        "controllerTest.present": {"type": "boolean", "initial": False},
        "controllerTest.name": {"type": "string", "initial": ""},
        "controllerTest.power": {"type": "number", "initial": 0},      # 1 battery, 2 charging, 3 charged, 4 wired
        "controllerTest.battery": {"type": "number", "initial": -1},   # percent, or -1 where it reports none
        "controllerTest.gyro": {"type": "boolean", "initial": False},
        "controllerTest.calibration": {"type": "number", "initial": 0},
        "controllerTest.motor": {"type": "number", "initial": 0},
        "controllerTest.steam": {"type": "boolean", "initial": False},
        **{f"controllerTest.{name}": {"type": "number", "initial": 0} for name in ("lx", "ly", "rx", "ry", "lt", "rt", "gyroX", "gyroY")},
    })
    setting = lambda cvar: {"state": f"cvar.{cvar}"}
    for cvar in ("in_joystickSouthpaw", "in_joystickLookDeadZone", "in_joystickLookOuterDeadZone", "in_joystickMoveDeadZone",
                 "in_joystickMoveOuterDeadZone", "in_joystickTriggerThreshold"):
        assert f"cvar.{cvar}" in doc.state, cvar
    southpaw = op("==", setting("in_joystickSouthpaw"), 1)
    # The look stick is the right one, or the left with Southpaw. A zone never
    # draws at nothing: a hairline ring stays at its centre.
    dead = lambda look: op("clamp", setting(f"in_joystick{'Look' if look else 'Move'}DeadZone"), 0.02, 0.95)
    outer = lambda look: op("-", 1, op("clamp", setting(f"in_joystick{'Look' if look else 'Move'}OuterDeadZone"), 0, 0.5))
    sides = lambda zone: (op("select", southpaw, zone(True), zone(False)), op("select", southpaw, zone(False), zone(True)))
    (left_dead, right_dead), (left_outer, right_outer) = sides(dead), sides(outer)

    face = lambda size, tint=WHITE, line=20: typeface("lowpixel", size, line, tint)
    name = label("test-name", PLACEHOLDER, {**absolute(left=PAD, top=34, width=SIDE - PAD - 12, height=20), **face(15),
                 "white-space": keyword("nowrap"), "overflow": keyword("hidden"), "pointer-events": keyword("none")})
    doc.bind("test-name.text", "test-name", "text", op("select", st("present"), st("name"), NO_CONTROLLER))
    power = label("test-power", PLACEHOLDER, {**absolute(left=SIDE, top=34, width=176, height=20), **face(14, [1, 1, 1, 0.6]),
                  "white-space": keyword("nowrap"), "overflow": keyword("hidden"), "pointer-events": keyword("none")})
    doc.bind("test-power.text", "test-power", "text", choose(st("power"), POWER))
    cell_w, cell_h = 44.0, 12.0
    battery = vector("test-battery", {**absolute(left=WIDTH - PAD - cell_w - 4, top=38, width=cell_w + 4, height=cell_h),
                                      "display": keyword("none"), "pointer-events": keyword("none")},
                     [path("cell", [(0, 0), (cell_w, 0), (cell_w, cell_h), (0, cell_h)], fill=solid([0, 0, 0, 0.35]),
                           stroke=stroke(solid([1, 1, 1, 0.45]), 1.5, minimum=1)),
                      path("cap", [(cell_w, 3), (cell_w + 3, 3), (cell_w + 3, cell_h - 3), (cell_w, cell_h - 3)], fill=solid([1, 1, 1, 0.45]))],
                     [group("test-battery-fill", {**absolute(left=2, top=2, width=0, height=cell_h - 4),
                                                  "background-color": colour(WHITE)})])
    has_battery = op(">=", st("battery"), 0)
    doc.bind("test-battery.display", "test-battery", "display", op("select", has_battery, "block", "none"))
    doc.bind("test-battery-fill.width", "test-battery-fill", "width",
             op("*", op("/", op("clamp", st("battery"), 0, 100), 100), cell_w - 4))

    top = 62.0
    sticks = [*stick(doc, "test-left", PAD, top, "lx", "ly", left_dead, left_outer, 13),
              *stick(doc, "test-right", PAD + STICK + 16, top, "rx", "ry", right_dead, right_outer, 14)]
    threshold = setting("in_joystickTriggerThreshold")
    triggers = [*trigger(doc, "test-lt", top + 2, "lt", 16, threshold), *trigger(doc, "test-rt", top + 36, "rt", 15, threshold)]

    box = 64.0
    gyro_top = top + 74
    reach = box / 2 - 6
    gyro_art = vector("test-gyro-box", {**absolute(left=0, top=0, width=box, height=box), "pointer-events": keyword("none")}, [
        path("frame", [(0, 0), (box, 0), (box, box), (0, box)], fill=solid([0, 0, 0, 0.35]), stroke=stroke(solid([1, 1, 1, 0.45]), 1.5, minimum=1)),
        path("across", [(4, box / 2), (box - 4, box / 2)], closed=False, stroke=stroke(solid([1, 1, 1, 0.12]), 1, minimum=1)),
        path("down", [(box / 2, 4), (box / 2, box - 4)], closed=False, stroke=stroke(solid([1, 1, 1, 0.12]), 1, minimum=1))])
    gyro_dot = vector("test-gyro-dot", {**absolute(left=0, top=0, width=box, height=box), "transform": transform(),
                                        "pointer-events": keyword("none")},
                      [circle_path("at", box / 2, box / 2, 5, fill=solid(rgb(ORANGE)), line=stroke(solid([0, 0, 0, 0.8]), 1, minimum=1))])
    doc.bind("test-gyro-dot.transform", "test-gyro-dot", "transform",
             [op("*", op("clamp", st("gyroX"), -1, 1), reach), op("*", op("clamp", st("gyroY"), -1, 1), reach), 1, 1, 0])
    gyro_text = label("test-gyro-state", PLACEHOLDER, {**absolute(left=box + 12, top=0, width=WIDTH - PAD - SIDE - box - 12, height=box),
                      **face(14, [1, 1, 1, 0.75], 16), "white-space": keyword("normal"), "overflow": keyword("hidden"),
                      "pointer-events": keyword("none")})
    doc.bind("test-gyro-state.text", "test-gyro-state", "text", choose(st("calibration"), CALIBRATION))
    gyro = group("test-gyro", {**absolute(left=SIDE, top=gyro_top, width=WIDTH - PAD - SIDE, height=box), "display": keyword("none"),
                               "pointer-events": keyword("none")}, [gyro_art, gyro_dot, gyro_text])
    doc.bind("test-gyro.display", "test-gyro", "display", op("select", st("gyro"), "block", "none"))

    motor = label("test-motor", PLACEHOLDER, {**absolute(left=SIDE, top=top + STICK + 4, width=WIDTH - PAD - SIDE, height=GLYPH),
                  **face(15, rgb(ORANGE), GLYPH), "white-space": keyword("nowrap"), "overflow": keyword("hidden"),
                  "pointer-events": keyword("none")})
    doc.bind("test-motor.text", "test-motor", "text", choose(st("motor"), MOTORS))

    strip_top = top + STICK + 4 + GLYPH + 12
    strip = glyphs("test-buttons", PAD, strip_top, WIDTH - 2 * PAD, [glyph(doc, key) for key in STRIP])
    hint_top, hint_h = strip_top + GLYPH + 8, 48.0
    hint = label("test-steam", STEAM, {**absolute(left=PAD, top=hint_top, width=WIDTH - 2 * PAD, height=hint_h),
                 **face(14, [1, 1, 1, 0.6], 16), "white-space": keyword("normal"), "overflow": keyword("hidden"),
                 "display": keyword("none"), "pointer-events": keyword("none")})
    doc.bind("test-steam.display", "test-steam", "display", op("select", st("steam"), "block", "none"))

    doc.session("controlsRumbleTest", "controlsRumbleTest")
    doc.events["controllerTestRumble"] = [{"op": "action", "action": "controlsRumbleTest"}]
    actions_u = round((hint_top + hint_h + 8) / U, 3)
    height_u = actions_u + 49
    return confirmation(doc, IDENT, TITLE, PLACEHOLDER, None, yes_event="controllerTestRumble", wide_title=True, height_u=height_u,
                        top_u=(480 - height_u) / 2, actions_u=actions_u,
                        contents=[name, power, battery, *sticks, *triggers, gyro, motor, strip, hint],
                        yes_key=RUMBLE, no_key=CLOSE, width_dp=WIDTH, action_w=230, action_text=17.0, initial=f"{IDENT}_yes")
