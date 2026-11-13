"""Remastered HUD groups beside the status bar (specification 14.5-14.12).

Weapon strip, pickup lines, the transmission display, the hit indicator,
interactive brackets, the boss bar and the timed notices, each from the
atlas construction (parts p14 and p18b) and the stock behaviour recorded in
.tmp findings hud.md/sp_hud.md. Events and state keys are the stock HUD's.
"""
from __future__ import annotations

import math

import build_retained_screens as ui
import hud_svg
from remastered_hud_base import (
    AND, BOUND_TEXT, NOT, OR, S, U, HudDocument, box, fitted_plate_path, frac_point, num, op, play, cancel, pts_path, rgb, rounded_rect, sel,
    set_state, shadowed, stroke, svg_path, text_box, tr, transform_value, u, vec, when)
from remastered_hud_status import WEAPON_GLYPHS, MP_ROSTER, full

PICKUP_TIMES = (3000, 250)        # hold, fade (atlas pickup lines)
WEAPON_SLOTS = 13                 # The Awakening's thirteen; the stock eleven use the first eleven
# The multiplayer strip keeps the weapon colour code (hud.mtr item colours, B.4).
MP_ITEM_COLOURS = {"item_gauntlet": (0, 0.85, 1), "item_machinegun": (1, 1, 0), "item_shotgun": (1, 0.5, 0),
                   "item_hyperblaster": (0, 0.45, 1), "item_grenade": (0.2, 0.56, 0.07), "item_nailgun": (0.6, 0.8, 0.8),
                   "item_rocket": (1, 0.2, 0), "item_railgun": (0, 1, 0), "item_lightning": (1, 1, 0.73),
                   "item_darkmatter": (0.77, 0, 1), "item_fire": (1, 0.75, 0.25), "item_corecannon": (1, 0.35, 0.78),
                   "item_freeze": (0.62, 0.92, 1), "item_blaster": (1, 1, 1)}
# Pickup icons by inv_icon base name: the atlas glyph and, in multiplayer,
# the item's own colour (health and armor pickups keep their colours).
PICKUP_GLYPHS = {
    "item_health": ("item_health", None), "item_armor": ("item_armor", None), "item_armorshard": ("item_armorshard", None),
    "item_generic": ("item_generic", None),
    "item_healthshard": ("item_health", (0.5, 1, 0.5)), "item_healthsmall": ("item_health", (1, 1, 0.2)),
    "item_healthlarge": ("item_health", (1, 0.5, 0)), "item_healthmega": ("item_health", (0, 0.5, 1)),
    "item_armorsmall": ("item_armor", (1, 1, 0)), "item_armorlarge": ("item_armor", (1, 0, 0)),
    "item_quaddamage": ("item_quaddamage", None), "item_haste": ("item_haste", None), "item_regen": ("item_regen", None),
    "item_invis": ("item_invis", None), "item_doubler": ("item_doubler", None), "item_guard": ("item_guard", None),
    "item_scout": ("item_scout", None), "item_ammoregen": ("item_ammoregen", None), "item_deadzone": ("item_deadzone", None),
    "item_adrenaline": ("item_adrenaline", None), "item_teamammo": ("item_teamammo", None),
    "item_teamhealth": ("item_teamhealth", None), "item_teamdamage": ("item_teamdamage", None),
    "item_fcarmorregen": ("item_fcarmorregen", None),
    "ammo_machinegun": ("ammo_machinegun", None), "ammo_shotgun": ("ammo_shotgun", None),
    "ammo_hyperblaster": ("ammo_hyperblaster", None), "ammo_grenade": ("ammo_grenade", None),
    "ammo_nailgun": ("ammo_nailgun", None), "ammo_rocket": ("ammo_rocket", None), "ammo_railgun": ("ammo_railgun", None),
    "ammo_lightning": ("ammo_lightning", None), "ammo_darkmatter": ("ammo_darkmatter", None),
    "ammo_gauntlet": ("ammo_gauntlet", None), "ammo_corecannon": ("ammo_corecannon", None),
    "ammo_freezegun": ("ammo_freezegun", None),
    "sb_flag_marine": ("sb_flag_marine", None), "sb_flag_strogg": ("sb_flag_strogg", None),
}
for _glyph in WEAPON_GLYPHS:
    PICKUP_GLYPHS.setdefault(_glyph, (_glyph, None))


# ============================================================ weapon strip

SP_STRIP = ["item_blaster", "item_machinegun", "item_shotgun", "item_hyperblaster", "item_grenade", "item_nailgun",
            "item_rocket", "item_railgun", "item_lightning", "item_darkmatter", "item_fire"]
AWAKENING_STRIP = SP_STRIP[:10] + ["item_corecannon", "item_fire", "item_freeze"]


STRIP_EASE = [0.25, 1, 0.5, 1]          # a quick settle (easeOutQuint)
STRIP_EASE_IN = [0.5, 0, 0.75, 0]      # a quick departure (easeInQuart)
STRIP_DRIFT = 4.0                      # u the Marine strip rises as it opens


def strip_glide_ms(steps: int, strogg: bool) -> int:
    """A longer move takes a little longer, never past 170 ms (180 ms for the
    Strogg cursor, which stretches on the way)."""
    return min(100 + 6 * abs(steps), 170) + (10 if strogg else 0)


def balanced(name: str, items: list) -> dict:
    """An n-ary sum or disjunction as a balanced tree (shallow expressions)."""
    while len(items) > 1:
        items = [op(name, items[j], items[j + 1]) if j + 1 < len(items) else items[j] for j in range(0, len(items), 2)]
    return items[0]


def weapon_strip(doc: HudDocument, *, strogg: bool = False, awakening: bool = False, y: float = 350.0) -> dict:
    """The strip (14.5): owned weapons in slot order with no gaps, 24.4 u
    plates on a 30 u pitch sheared about their own centres, upright 18 u
    icons, the pending weapon orange inside a hard 1.6 u ring, an empty
    weapon dim under a red prohibition sign. Remastered sets the name above
    the slots and the strip above the pickup lines.

    Remastered motion (14.5, version 1.44): the strip opens and closes
    instead of cutting, one ring travels from the old choice to the new one,
    a refused choice shakes the ring red, and a tick marks the weapon a
    quick switch returns to. The Marine strip drifts and settles; the Strogg
    strip, after stroggification, tunes in and collapses like a neural link,
    its ring stretches as it travels and sends an impulse when it lands, and
    a refusal glitches in hard steps."""
    mp = S("mp")
    roster = AWAKENING_STRIP if awakening else SP_STRIP
    count = len(roster)
    for i in range(count):
        doc.boolean(f"weapon{i}")
        doc.number(f"weapon{i}_index")
        doc.number(f"weapon{i}_ammo")
        doc.string(f"weapon{i}_icon")
    doc.number("weaponcount")
    doc.number("player_weapon", -1)
    doc.number("player_idealweapon", -1)
    doc.number("player_lastweapon", -1)
    doc.number("weapon_refused", -1)
    doc.string("weaponname")
    doc.boolean("oq4.strip")
    # The ring's last resting place, so the next change knows where it left.
    doc.number("oq4.stripPos", -1)
    doc.number("oq4.stripTo", -1)
    doc.number("oq4.stripCount", 0)
    doc.boolean("oq4.stripRefuse")
    if strogg:
        doc.boolean("oq4.stripPulse")
        doc.boolean("oq4.stripScan")
    selected_colour = rgb("#FFCC00") if strogg else rgb("#FF8000")
    other_colour = rgb("#F59512", 0.6) if strogg else rgb("#A8A360", 0.6)
    ring_colour = rgb("#F2AD0B", 0.6) if strogg else rgb("#B2CC80")
    refuse_colour = rgb("#FF3A1A") if strogg else rgb("#E0442E")
    k = 0.22 if strogg else -0.22
    ideal = S("player_idealweapon")
    # Where the pending weapon sits in the strip (its weaponN_index).
    selected_pos = balanced("+", [sel(op("==", ideal, i), S(f"weapon{i}_index"), 0) for i in range(count)])
    owned_selected = balanced("||", [AND(op("==", ideal, i), S(f"weapon{i}")) for i in range(count)])
    # The weapon a quick switch returns to: the one in hand while a change is
    # pending, else the one before it.
    returns_to = sel(op("!=", S("player_weapon"), ideal), S("player_weapon"), S("player_lastweapon"))

    # The strip is a box about its own middle, so the Strogg variant can
    # tune in and collapse about the row; children sit `top` u above y.
    top = 22.0
    sy = top
    slots = []
    for i in range(count):
        sid = f"slot{i}"
        selected = op("==", ideal, i)
        empty = op("==", S(f"weapon{i}_ammo"), 0)
        plate = vec(sid + "-plate", [pts_path("plate", [(x * U, yy * U) for x, yy in rounded_rect(0.8, 0.8, 24.4, 24.4, 1.0, k, 13)],
                                              fill=ui.solid([0, 0, 0, 0.5]))])
        icons = []
        # Each slot holds its roster weapon; multiplayer's first slot is the
        # gauntlet (the stock strip's weaponN_icon follows the same order).
        sp_glyph = roster[i]
        mp_glyph = MP_ROSTER[i] if not awakening and i < len(MP_ROSTER) else None
        sp_on = vec_icon(f"{sid}-dim", sp_glyph, other_colour)
        sp_sel = vec_icon(f"{sid}-sel", sp_glyph, selected_colour)
        doc.show(sp_on["id"], AND(NOT(mp), OR(NOT(selected), empty)))
        doc.show(sp_sel["id"], AND(NOT(mp), selected, NOT(empty)))
        icons += [sp_on, sp_sel]
        if mp_glyph:
            colour = list(MP_ITEM_COLOURS[mp_glyph])
            mp_dim = vec_icon(f"{sid}-mpdim", mp_glyph, [*colour, 0.6])
            mp_full = vec_icon(f"{sid}-mpsel", mp_glyph, [*colour, 1.0])
            doc.show(mp_dim["id"], AND(mp, OR(NOT(selected), empty)))
            doc.show(mp_full["id"], AND(mp, selected, NOT(empty)))
            icons += [mp_dim, mp_full]
        sign = vec(sid + "-noammo", [
            pts_path("ring", [(13 * U + 7.4 * U * math.cos(t * math.pi / 24), 13 * U + 7.4 * U * math.sin(t * math.pi / 24))
                              for t in range(48)], stroke=stroke(rgb("#A50000"), u(2.3))),
            pts_path("slash", [(18.2 * U, 7.8 * U), (7.8 * U, 18.2 * U)], closed=False, stroke=stroke(rgb("#A50000"), u(2.3)))],
            {"display": ui.keyword("none"), "transform": transform_value()})
        doc.show(sign["id"], empty)
        # The return tick under the plate: a short bar leaning with the
        # Marine plates, a small upward chevron on the Strogg strip.
        if strogg:
            tick_paths = [pts_path("tick", [(10.2 * U, 29.4 * U), (13 * U, 26.8 * U), (15.8 * U, 29.4 * U)], closed=False,
                                   stroke=stroke(ring_colour, u(1.3), join="miter"))]
        else:
            tick_paths = [pts_path("tick", [(x * U, yy * U) for x, yy in rounded_rect(8.5, 26.9, 9.0, 1.5, 0.4, k, 27.65, 2)],
                                   fill=ui.solid([*ring_colour[:3], 0.75]))]
        tick = vec(sid + "-last", tick_paths, {"display": ui.keyword("none")})
        doc.show(tick["id"], AND(op("==", returns_to, i), NOT(selected), op(">=", S("player_lastweapon"), 0)))
        slot = box(sid, 0, u(sy), u(26), u(26),
                   [plate, full(sid + "-icons", icons, transform=transform_value()), sign, tick])
        # switchLeft = (640 - (weaponcount 30 - 2)) / 2, plus 30 u per index.
        doc.bind(sid + ".left", sid, "left",
                 op("*", op("+", op("-", 321, op("*", 15, S("weaponcount"))), op("*", 30, S(f"weapon{i}_index"))), U))
        doc.show(sid, S(f"weapon{i}"))
        slots.append(slot)

    # One ring for the pending weapon, travelling between slots. Its glide
    # child carries the travel, the shake child the refusal.
    ring_points = [(x * U, yy * U) for x, yy in rounded_rect(1.6, 1.6, 22.8, 22.8, 1.0, k, 13)]
    ring = vec("strip-ring", [pts_path("ring", ring_points, stroke=stroke(ring_colour, u(1.6), join="round"))])
    refuse = vec("strip-refuse", [pts_path("ring", ring_points, stroke=stroke(refuse_colour, u(1.6), join="round"))],
                 {"display": ui.keyword("none"), "opacity": num(0)})
    doc.show(refuse["id"], S("oq4.stripRefuse"))
    cursor_children = [ring, refuse]
    if strogg:
        pulse = vec("strip-pulse", [pts_path("ring", ring_points, stroke=stroke(selected_colour, u(1.2), join="round"))],
                    {"display": ui.keyword("none"), "opacity": num(0), "transform": transform_value()})
        doc.show(pulse["id"], S("oq4.stripPulse"))
        cursor_children.append(pulse)
    shake = full("strip-shake", cursor_children, transform=transform_value())
    glide = full("strip-glide", [shake], transform=transform_value())
    cursor = box("strip-cursor", 0, u(sy), u(26), u(26), [glide])
    doc.bind("strip-cursor.left", "strip-cursor", "left",
             op("*", op("+", op("-", 321, op("*", 15, S("weaponcount"))), op("*", 30, selected_pos)), U))
    doc.show("strip-cursor", owned_selected)

    if strogg:
        name = shadowed("strip-name-t", "", face="r_strogg", size=14.4, colour=rgb("#FCFFC8"), baseline=u(sy - 7), center=u(320),
                        width=u(600))
    else:
        name = shadowed("strip-name-t", "", face="marine", size=18, colour=rgb("#B0C891"), baseline=u(sy - 7), center=u(320),
                        width=u(600))
    for node in name:
        doc.bind(node["id"] + ".text", node["id"], "text", S("weaponname"))
    name_box = full("strip-name", name, transform=transform_value(), opacity=num(1))
    children = [name_box] + slots + [cursor]
    if strogg:
        # The tune-in's scan: a hairline as wide as the row (8 u past each
        # end plate) that sweeps down through it.
        line = {"id": "line", "fill": ui.solid([*selected_colour[:3], 1.0]), "commands": [
            {"id": "c0", "op": "move", "points": [frac_point(0, 0, 0)]},
            {"id": "c1", "op": "line", "points": [frac_point(1, 0, 0)]},
            {"id": "c2", "op": "line", "points": [frac_point(1, 0, u(0.9))]},
            {"id": "c3", "op": "line", "points": [frac_point(0, 0, u(0.9))]},
            {"id": "c4", "op": "close"}]}
        scan = vec("strip-scan", [line], {"display": ui.keyword("none"), "opacity": num(0), "transform": transform_value()})
        doc.show(scan["id"], S("oq4.stripScan"))
        span = box("strip-scan-span", 0, 0, u(400), u(1), [scan])
        doc.bind("strip-scan-span.left", "strip-scan-span", "left", op("*", op("-", 313, op("*", 15, S("weaponcount"))), U))
        doc.bind("strip-scan-span.width", "strip-scan-span", "width", op("*", op("+", 12, op("*", 30, S("weaponcount"))), U))
        children.append(span)
    # Closed values are the base: the first opening moves from them.
    closed = transform_value(sx=1.03, sy=0.3) if strogg else transform_value(ty=u(STRIP_DRIFT))
    # The hold's clock lives on a node nothing else animates, so a takeover
    # of the strip's own tracks never completes it early.
    children.append(ui.group("strip-timer", {**ui.FULL, "display": ui.keyword("none"), "opacity": num(1)}))
    strip = box("strip", 0, u(y - top), u(640), u(48), children,
                opacity=num(0), transform=closed)
    # The weapon strip setting (13.9) keeps it hidden.
    doc.show("strip", AND(S("oq4.strip"), S("hud.strip")))

    # Opening and closing start from wherever the strip is (no authored
    # first key), so a change during the close turns it back smoothly.
    rest = transform_value()
    if strogg:
        open_tracks = [tr("strip", "opacity", [(0, num(0)), (90, num(1))]),
                       tr("strip", "transform", [(0, closed, STRIP_EASE), (140, rest)])]
        close_tracks = lambda ms: [tr("strip", "opacity", [(0, num(1), STRIP_EASE_IN), (ms, num(0))]),
                                   tr("strip", "transform", [(0, rest, STRIP_EASE_IN), (ms, transform_value(sx=1.02, sy=0.35))])]
    else:
        open_tracks = [tr("strip", "opacity", [(0, num(0)), (110, num(1))]),
                       tr("strip", "transform", [(0, closed, STRIP_EASE), (140, rest)])]
        close_tracks = lambda ms: [tr("strip", "opacity", [(0, num(1)), (ms, num(0))]),
                                   tr("strip", "transform", [(0, rest, STRIP_EASE_IN), (ms, transform_value(ty=u(3)))])]
    opening = doc.timelines.add("strip-open", 140, open_tracks)
    closing = doc.timelines.add("strip-close", 150, close_tracks(150), complete="stripDone")
    closing_fast = doc.timelines.add("strip-close-fast", 90, close_tracks(90), complete="stripDone")
    hold = doc.timeline("strip-hold", 1000, [tr("strip-timer", "opacity", [(0, num(1)), (1000, num(1))])], complete="stripClose",
                        essential=True)

    # The ring's travel: one timeline per distance, so it starts exactly on
    # the slot it left.
    delta = op("-", S("oq4.stripTo"), S("oq4.stripPos"))
    glides = []
    for steps in [d for d in range(-(count - 1), count) if d]:
        ms = strip_glide_ms(steps, strogg)
        start = u(-30 * steps)
        if strogg:
            stretch = 1 + min(0.05 * abs(steps), 0.22)
            keys = [(0, transform_value(tx=start), [0.4, 0, 0.6, 1]),
                    (round(ms * 0.45), transform_value(tx=start * 0.3, sx=stretch), STRIP_EASE), (ms, rest)]
        else:
            keys = [(0, transform_value(tx=start), STRIP_EASE), (ms, rest)]
        ident = doc.timeline(f"strip-glide{'n' if steps < 0 else 'p'}{abs(steps)}", ms, [tr("strip-glide", "transform", keys)])
        glides.append(when(op("==", delta, steps), [play(ident)]))
    # The name follows the ring in from the side it came from.
    names = {}
    for side, sign_ in (("p", 1), ("n", -1)):
        if strogg:
            # A hard-stepped slip, like a signal finding its channel.
            keys = [(0, transform_value(tx=u(3 * sign_))), (40, transform_value(tx=u(3 * sign_))),
                    (41, transform_value(tx=u(-1.2 * sign_))), (80, transform_value(tx=u(-1.2 * sign_))), (81, rest), (120, rest)]
            fade = [(0, num(0.45)), (40, num(0.45)), (41, num(0.8)), (80, num(0.8)), (81, num(1)), (120, num(1))]
        else:
            keys = [(0, transform_value(tx=u(4 * sign_)), STRIP_EASE), (130, rest)]
            fade = [(0, num(0.3)), (130, num(1))]
        names[side] = doc.timeline(f"strip-name-{side}", 130 if not strogg else 120,
                                   [tr("strip-name", "transform", keys), tr("strip-name", "opacity", fade)])
    landing = [when(op(">", delta, 0), [play(names["p"])]), when(op("<", delta, 0), [play(names["n"])])]
    if strogg:
        # The impulse: an outline leaving the ring where it lands.
        impulse = doc.timeline("strip-impulse", 220, [
            tr("strip-pulse", "transform", [(0, transform_value(), STRIP_EASE), (220, transform_value(sx=1.45, sy=1.45))]),
            tr("strip-pulse", "opacity", [(0, num(0.8)), (220, num(0))])], complete="stripPulseDone")
        doc.on("stripPulseDone", set_state(**{"oq4.stripPulse": False}))
        landing.append(when(op("!=", delta, 0), [set_state(**{"oq4.stripPulse": True}), play(impulse)]))
        scan_line = doc.timeline("strip-scan-sweep", 150, [
            tr("strip-scan", "transform", [(0, transform_value(ty=u(-6))), (150, transform_value(ty=u(50)))]),
            tr("strip-scan", "opacity", [(0, num(0)), (30, num(0.55)), (110, num(0.4)), (150, num(0))])], complete="stripScanDone")
        doc.on("stripScanDone", set_state(**{"oq4.stripScan": False}))
        tune_in = [set_state(**{"oq4.stripScan": True}), play(scan_line)]
    else:
        # The Marine icon settles into the ring.
        for i in range(count):
            pop = doc.timeline(f"strip-pop{i}", 150, [tr(f"slot{i}-icons", "transform",
                                                         [(0, transform_value(sx=1.16, sy=1.16), STRIP_EASE), (150, rest)])])
            landing.append(when(AND(op("==", ideal, i), op("!=", delta, 0)), [play(pop)]))
        tune_in = []

    # A refusal: the ring shakes and flashes red; an empty weapon's sign
    # throbs once to say why.
    if strogg:
        shake_keys = [(0, transform_value(tx=u(2.2))), (35, transform_value(tx=u(2.2))), (36, transform_value(tx=u(-1.8), sx=1.06)),
                      (70, transform_value(tx=u(-1.8), sx=1.06)), (71, transform_value(tx=u(1.0))), (105, transform_value(tx=u(1.0))),
                      (106, rest), (140, rest)]
        flash = [(0, num(1)), (50, num(1)), (240, num(0))]
        shake_ms = 140
    else:
        shake_keys = [(0, rest, STRIP_EASE), (30, transform_value(tx=u(-2.5))), (70, transform_value(tx=u(2.0))),
                      (110, transform_value(tx=u(-1.2))), (150, transform_value(tx=u(0.6))), (190, rest)]
        flash = [(0, num(1)), (70, num(1)), (240, num(0))]
        shake_ms = 190
    shake_tl = doc.timeline("strip-refuse-shake", shake_ms, [tr("strip-shake", "transform", shake_keys)])
    # The red is a colour cue, not motion: reduced motion keeps it.
    flash_tl = doc.timeline("strip-refuse-flash", 240, [tr("strip-refuse", "opacity", flash)], complete="stripRefuseDone",
                            essential=True)
    doc.on("stripRefuseDone", set_state(**{"oq4.stripRefuse": False}))
    throbs = []
    for i in range(count):
        throb = doc.timeline(f"strip-throb{i}", 180, [tr(f"slot{i}-noammo", "transform",
                                                         [(0, transform_value(sx=1.35, sy=1.35), STRIP_EASE), (180, rest)])])
        throbs.append(when(AND(op("==", S("weapon_refused"), i), S(f"weapon{i}")), [play(throb)]))

    # Shown in the frame of a change, never before the first weapon or while
    # the HUD is hidden; closed 1000 ms after the latest change or quickly on
    # firing (hud.gui:194-255).
    def show_steps() -> list:
        return [*([when(NOT(S("oq4.strip")), tune_in)] if tune_in else []),
                set_state(**{"oq4.strip": True, "oq4.stripPos": S("oq4.stripTo"), "oq4.stripCount": S("weaponcount")}),
                play(opening), play(hold)]
    doc.on("weaponChange", when(AND(op(">=", S("player_lastweapon"), 0), NOT(S("oq4.hidden"))), [
        set_state(**{"oq4.stripTo": selected_pos}),
        # The ring travels only along a strip already on screen and unchanged.
        when(AND(S("oq4.strip"), op("==", S("weaponcount"), S("oq4.stripCount"))), glides + landing),
        *show_steps()],
        [set_state(**{"oq4.strip": False})]))
    doc.on("weaponRefused", when(AND(op(">", S("weaponcount"), 0), NOT(S("oq4.hidden"))), [
        set_state(**{"oq4.stripTo": selected_pos, "oq4.stripRefuse": True}),
        play(shake_tl), play(flash_tl), *throbs, *show_steps()]))
    doc.on("weaponFire", when(S("oq4.strip"), [cancel(hold, "hold"), play(closing_fast)]))
    doc.on("hideHud", cancel(hold, "hold"), play(closing))
    doc.on("stripClose", play(closing))
    doc.on("stripDone", set_state(**{"oq4.strip": False}))
    return strip


def vec_icon(ident: str, glyph: str, colour: list, x: float = 13 * U, y: float = 4 * U, h: float = 18 * U) -> dict:
    return full(ident, hud_svg.icon_nodes(glyph, ident + "-g", x, y, h, tint=colour, anchor="middle"), display=ui.keyword("none"))


# ============================================================ pickup lines

def pickup_lines(doc: HudDocument, *, strogg: bool = False, multiply_key: str = "#str_231060") -> dict:
    """Remastered pickups (14.8): up to two lines centred over the status bar,
    newest nearest the gauges, repeats merged into one line with a count;
    each line holds 3000 ms and fades over 250 ms. A line is a rounded black
    0.50 sheared plate no wider than its content (min 96 u), the item icon
    at 80% and its name in Marine 14 dp."""
    doc.string("itemtext")
    doc.string("itemicon")
    doc.number("oq4.pickNewest")
    hold, fade = PICKUP_TIMES
    rows = (401.0, 377.0)
    text_colour = rgb("#FCFFC8") if strogg else rgb("#DCEAC2")
    icon_colour = rgb("#FFCC00", 0.6) if strogg else rgb("#AAAC6F", 0.8)
    face, size = ("r_strogg", 11.5) if strogg else ("marine", 14.4)
    lines = []
    for slot in (0, 1):
        key = f"oq4.pick{slot}"
        doc.string(key + ".text")
        doc.string(key + ".icon")
        doc.number(key + ".count")
        doc.boolean(key + ".live")
        lid = f"pickup{slot}"
        # The plate: 21 u tall at the row's y + 1.5, sheared about its centre.
        # The plate is the line box's own vector: 21 u tall from 1.5 u down,
        # sheared about its centre, spanning whatever width the text needs.
        plate = fitted_plate_path("plate", u(21), k=0.22 if strogg else -0.22, r=u(1), fill=ui.solid([0, 0, 0, 0.5]),
                                  top=u(1.5))
        icons = []
        for name, (glyph, colour) in PICKUP_GLYPHS.items():
            tint = icon_colour if colour is None else [*colour, 0.8]
            holder = full(f"{lid}-{name}", hud_svg.icon_nodes(glyph, f"{lid}-{name}-g", u(6), u(4), u(16), tint=tint),
                          display=ui.keyword("none"))
            doc.show(holder["id"], op("==", S(key + ".icon"), name))
            icons.append(holder)
        label = ui.label(lid + "-text", BOUND_TEXT, {**ui.typeface(face, size, u(24), text_colour), "position": ui.keyword("relative"),
                                             "white-space": ui.keyword("nowrap"), "display": ui.keyword("inline-block")})
        doc.bind(lid + "-text.text", label["id"], "text", S(key + ".text"))
        count_sign = ui.label(lid + "-times", multiply_key, {**ui.typeface(face, size, u(24), text_colour),
                                                              "position": ui.keyword("relative"), "margin-left": ui.length(u(6)),
                                                              "white-space": ui.keyword("nowrap"), "display": ui.keyword("inline-block")})
        count = ui.label(lid + "-count", BOUND_TEXT, {**ui.typeface(face, size, u(24), text_colour), "position": ui.keyword("relative"),
                                              "white-space": ui.keyword("nowrap"), "display": ui.keyword("inline-block")})
        doc.bind(lid + "-count.text", count["id"], "text", op("numberText", S(key + ".count")))
        doc.show(count_sign["id"], op(">", S(key + ".count"), 1), "inline-block")
        doc.show(count["id"], op(">", S(key + ".count"), 1), "inline-block")
        content = ui.vector(lid + "-content", {"position": ui.keyword("relative"), "display": ui.keyword("block"),
                                               "height": ui.length(u(24)), "padding-left": ui.length(u(25)),
                                               "padding-right": ui.length(u(7)), "min-width": ui.length(u(96 - 32)),
                                               "white-space": ui.keyword("nowrap")},
                            [plate], [full(lid + "-icons", icons), label, count_sign, count])
        row = ui.group(lid, {**ui.absolute(left=0, top=u(rows[0]), width=u(640), height=u(24)), "display": ui.keyword("none"),
                             "opacity": num(1), "flex-direction": ui.keyword("row"), "justify-content": ui.keyword("center")},
                       [content])
        doc.bind(lid + ".display", lid, "display", sel(S(key + ".live"), "flex", "none"))
        doc.bind(lid + ".top", lid, "top", sel(op("==", S("oq4.pickNewest"), slot), u(rows[0]), u(rows[1])))
        doc.timeline(lid + "-hold", hold + fade, [tr(lid, "opacity", [(0, num(1)), (hold, num(1)), (hold + fade, num(0))])],
                     complete=lid + "Done", essential=True)
        doc.on(lid + "Done", {"op": "setState", "values": {key + ".live": False}})
        lines.append(row)

    def fill(slot):
        key = f"oq4.pick{slot}"
        return [{"op": "setState", "values": {"oq4.pickNewest": slot, key + ".text": S("itemtext"), key + ".icon": S("itemicon"),
                                              key + ".count": 1, key + ".live": True}}, play(f"pickup{slot}-hold")]

    def merge(slot):
        key = f"oq4.pick{slot}"
        return [{"op": "setState", "values": {key + ".count": op("+", S(key + ".count"), 1)}}, play(f"pickup{slot}-hold")]
    repeat = lambda slot: AND(S(f"oq4.pick{slot}.live"), op("==", S("itemtext"), S(f"oq4.pick{slot}.text")))
    doc.on("itemPickup", when(op("==", S("oq4.pickNewest"), 0),
                              [when(repeat(0), merge(0), fill(1))],
                              [when(repeat(1), merge(1), fill(0))]))
    group = full("pickups", lines)
    # The pickup messages setting (13.9) hides them.
    doc.bind("pickups.setting", "pickups", "display", sel(S("hud.pickups"), "block", "none"))
    return group


# ============================================================ transmission

WAVE_RECT = (513.0, 7.0, 41.0, 25.0)             # the stock waveform window
WAVE_MASK = "M526.45 8.56H545.67V30.44H522.61Z"   # waveform_mask: a right trapezoid
ST_WAVE_RECT = (515.0, 7.0, 41.0, 25.0)
ST_WAVE_MASK = "M522 8.56H544.5L548.3 30.44H525.9Z"


def transmission(doc: HudDocument, *, strogg: bool = False, emblem: str = "radio_rhino") -> dict:
    """The transmission display (14.8): INCOMING / TRANSMISSION on a sheared
    rounded bar beside the installed legacy waveform with its canned UV motion.
    When the line addresses the player, the squad stencil and its plate
    shrink into place over 500 ms. Remastered anchors it top-trailing."""
    doc.boolean("rhinochatter")
    doc.boolean("oq4.radio")
    k = 0.22 if strogg else -0.22
    bar_x = 522.8 if strogg else 521.8
    bar = vec("radio-bar", [pts_path("bar", [(x * U, y * U) for x, y in rounded_rect(bar_x, 6.8, 109.4, 24.4, 1.5, k, 19)],
                                     fill=ui.solid([0, 0, 0, 0.498]))])
    rx, ry, rw, rh = ST_WAVE_RECT if strogg else WAVE_RECT
    mask_svg = ST_WAVE_MASK if strogg else WAVE_MASK
    colour = rgb("#FFCC00") if strogg else rgb("#FF8000")
    from hud_waveform import waveform
    graph = waveform(doc, "radio-graph", rw, rh, colour)
    graph["properties"].update(left=ui.length(u(rx)), top=ui.length(u(ry)))
    wave = full("radio-wave", [graph], mask={"paths": [svg_path("mask", mask_svg, fill=ui.solid([1, 1, 1, 1]))]})
    if strogg:
        labels = [text_box("radio-l1", "#str_200272", face="r_strogg", size=11.5, colour=[1, 1, 1, 1], baseline=u(19), x=u(551)),
                  text_box("radio-l2", "#str_200273", face="r_strogg", size=11.5, colour=[1, 1, 1, 1], baseline=u(26), x=u(552))]
    else:
        labels = [text_box("radio-l1", "#str_200272", face="marine", size=14.4, colour=[1, 1, 1, 1], baseline=u(19), x=u(547)),
                  text_box("radio-l2", "#str_200273", face="marine", size=14.4, colour=[1, 1, 1, 1], baseline=u(26), x=u(547))]
    # The squad stencil: plate 46x47 -> 26x26 u and stencil 42x39 -> 22x22 u
    # (hud.gui rhino / d_rhino), both sheared, over 500 ms.
    p0, p1 = ((474, 6, 46, 47), (492, 7, 26, 26)) if strogg else ((468, 5, 46, 47), (493, 6, 26, 26))
    e0, e1 = ((477, 10, 42, 39), (494, 9, 22, 22)) if strogg else ((471, 9, 42, 39), (495, 8, 22, 22))
    inset = 0.031
    px, py, pw, ph = p1
    plate = vec("rhino-plate", [pts_path("plate", [(x * U, y * U) for x, y in rounded_rect(px + pw * inset, py + ph * inset,
                                                                                           pw * (1 - 2 * inset), ph * (1 - 2 * inset),
                                                                                           1.8, k, py + ph / 2)],
                                          fill=ui.solid([0, 0, 0, 0.4]))])
    ex, ey, ew, eh = e1
    stencil_colour = rgb("#FFCC00") if strogg else rgb("#FFFF00")
    # Shear the stencil about its own centre with an exact transform.
    from remastered_hud_base import shear_group
    stencil = shear_group("rhino-stencil", u(ex), u(ey), u(ew), u(eh), k,
                          hud_svg.icon_nodes(emblem, "rhino-g", 0, 0, u(eh), tint=stencil_colour))

    def shrink(ident, start, end):
        sx = start[2] / end[2]
        sy = start[3] / end[3]
        dx = (start[0] + start[2] / 2) - (end[0] + end[2] / 2)
        dy = (start[1] + start[3] / 2) - (end[1] + end[3] / 2)
        return transform_value(u(dx), u(dy), sx, sy)
    plate_box = box("rhino-plate-box", u(p1[0]), u(p1[1]), u(p1[2]), u(p1[3]), [plate], transform=ui.transform())
    # The plate path is in canvas dp; move it into the box's own frame.
    for path in plate["paths"]:
        for command in path["commands"]:
            if "points" in command:
                command["points"] = [[round(x - u(p1[0]), 4), round(y - u(p1[1]), 4)] for x, y in command["points"]]
    stencil_box = box("rhino-stencil-box", 0, 0, u(640), u(480), [stencil], transform=ui.transform())
    rhino = full("rhino", [plate_box, stencil_box], display=ui.keyword("none"))
    doc.show("rhino", S("rhinochatter"))
    # The stencil box spans the canvas; its pivot is the canvas centre.
    def shrink_about_canvas(start, end):
        sx = start[2] / end[2]
        sy = start[3] / end[3]
        cx, cy = 320.0, 240.0
        ecx, ecy = end[0] + end[2] / 2, end[1] + end[3] / 2
        scx, scy = start[0] + start[2] / 2, start[1] + start[3] / 2
        # p' = c + s (p - c) + t must map the end centre onto the start centre.
        tx = scx - (cx + sx * (ecx - cx))
        ty = scy - (cy + sy * (ecy - cy))
        return transform_value(u(tx), u(ty), sx, sy)
    doc.timeline("rhino-shrink", 500, [
        tr("rhino-plate-box", "transform", [(0, shrink("p", p0, p1)), (500, transform_value())]),
        tr("rhino-stencil-box", "transform", [(0, shrink_about_canvas(e0, e1)), (500, transform_value())]),
    ])
    group = full("radio", [rhino, bar, wave] + labels, display=ui.keyword("none"))
    doc.show("radio", S("oq4.radio"))
    doc.on("radioChatterUp", {"op": "setState", "values": {"oq4.radio": True}}, play("rhino-shrink"))
    doc.on("radioChatterDown", {"op": "setState", "values": {"oq4.radio": False}})
    return group


# ============================================================ hit indicator

def flash_cap(doc: HudDocument, leaves, peak: float, cap: float = 0.4) -> None:
    """Reduced flashing (ui_reducedFlashing, 13.10) scales a flash whose
    timeline peaks at `peak` so it never exceeds `cap`; each leaf needs a
    base opacity of 1."""
    doc.cvar("hud.reducedFlash", "ui_reducedFlashing")
    for leaf in leaves:
        doc.bind(leaf + ".flashCap", leaf, "opacity", sel(S("hud.reducedFlash"), round(cap / peak, 5), 1))


def hit_indicator(doc: HudDocument) -> dict:
    """The damage direction (14.7): a soft #FF1A1A dome of light about 227 dp
    from the centre, rotated toward the source, from 0.70 to nothing over
    500 ms; Remastered scales it with the HUD and adds a chevron at its peak
    so direction never depends on colour."""
    doc.number("hitdir")
    # The dome: an ellipse 150x52 u at (320, 89), a circle-gradient node
    # flattened about its own centre.
    r = 150.0
    disc = vec("hit-dome-disc", [{
        "id": "dome", "commands": hud_svg.commands(f"M{2 * r} {r}A{r} {r} 0 1 1 0 {r}A{r} {r} 0 1 1 {2 * r} {r}Z", (U, 0, 0, U, 0, 0)),
        "fill": {"type": "radial", "from": [u(r), u(r)], "to": [u(2 * r), u(r)],
                 "stops": [{"at": 0, "color": ui.colour(rgb("#FF1A1A", 1))},
                           {"at": 0.55, "color": ui.colour(rgb("#FF1A1A", 0.45))},
                           {"at": 1, "color": ui.colour(rgb("#FF1A1A", 0))}]},
        "blend": "additive"}], {**ui.absolute(left=u(320 - r), top=u(89 - r), width=u(2 * r), height=u(2 * r)),
                                "transform": transform_value(0, 0, 1, 52 / 150), "opacity": num(1)})
    chevron = vec("hit-chevron", [pts_path("chevron", [(u(311), u(52)), (u(320), u(44)), (u(329), u(52))], closed=False,
                                           stroke=stroke(rgb("#FF1A1A", 0.9), u(3.2), join="miter", cap="butt"),
                                           blend="additive")], {"opacity": num(1)})
    # Reduced flashing (13.10, 14.10) caps the peak at 0.40 on the leaves,
    # where opacity is a vertex tint.
    flash_cap(doc, ("hit-dome-disc", "hit-chevron"), 0.7)
    fading = full("hit-fade", [disc, chevron], opacity=num(0))
    turning = full("hit", [fading], transform=ui.transform())
    # The damage direction setting (13.9) hides it.
    doc.bind("hit.setting", "hit", "display", sel(S("hud.damageDir"), "block", "none"))
    doc.bind("hit.rotation", "hit", "transform", [0, 0, 1, 1, op("-", 0, S("hitdir"))])
    doc.timeline("hit-flash", 500, [tr("hit-fade", "opacity", [(0, num(0.7)), (500, num(0))])], essential=True)
    doc.on("playerHit", play("hit-flash"))
    return turning


# ============================================================ brackets

def interactive_brackets(doc: HudDocument) -> dict:
    """Open #AFDE90 brackets around the focused world GUI's projected bounds
    plus 10 u, labelled Interactive (14.8, hud.gui:2136): 2 u uprights with
    5x2 u prongs. The bounds arrive in the centred 4:3 HUD space, so this box
    is the unscaled canvas."""
    for key in ("bracket_left", "bracket_top", "bracket_width", "bracket_height"):
        doc.number(key)
    doc.boolean("oq4.brackets")
    colour = rgb("#AFDE90")

    def f(fx, dx, y):
        return [{"fraction": fx, "dp": round(dx, 4)}, y]

    def fy(fx, dx, fyv, dy):
        return [{"fraction": fx, "dp": round(dx, 4)}, {"fraction": fyv, "dp": round(dy, 4)}]

    def rect(ident, x0, y0, x1, y1):
        return {"id": ident, "fill": ui.solid(colour), "commands": [
            {"id": "a", "op": "move", "points": [fy(*x0, *y0)]}, {"id": "b", "op": "line", "points": [fy(*x1, *y0)]},
            {"id": "c", "op": "line", "points": [fy(*x1, *y1)]}, {"id": "d", "op": "line", "points": [fy(*x0, *y1)]},
            {"id": "e", "op": "close"}]}
    two, seven = u(2), u(7)
    paths = [rect("left", (0, 0), (0, 0), (0, two), (1, 0)),
             rect("lt", (0, two), (0, 0), (0, seven), (0, two)),
             rect("lb", (0, two), (1, -two), (0, seven), (1, 0)),
             rect("right", (1, -two), (0, 0), (1, 0), (1, 0)),
             rect("rt", (1, -seven), (0, 0), (1, -two), (0, two)),
             rect("rb", (1, -seven), (1, -two), (1, -two), (1, 0))]
    frame = vec("brackets-frame", paths, {**ui.absolute(left=0, top=0, width=u(20), height=u(20))})
    label = ui.label("brackets-label", "#str_200277", {**ui.typeface("marine", 18, 18, colour), **ui.absolute(left=u(-50), top=u(-30), width=u(140), height=u(30)),
                                                        "text-align": ui.keyword("center"), "white-space": ui.keyword("nowrap")})
    group = ui.group("brackets", {**ui.absolute(left=0, top=0, width=u(20), height=u(20)), "display": ui.keyword("none"),
                                   "opacity": num(1)}, [frame, label])
    left = op("*", op("-", S("bracket_left"), 10), U)
    top = op("*", op("-", S("bracket_top"), 10), U)
    width = op("*", op("max", 0, op("+", S("bracket_width"), 20)), U)
    height = op("*", op("max", 0, op("+", S("bracket_height"), 20)), U)
    doc.bind("brackets.left", "brackets", "left", left)
    doc.bind("brackets.top", "brackets", "top", top)
    doc.bind("brackets.width", "brackets", "width", width)
    doc.bind("brackets.height", "brackets", "height", height)
    doc.bind("brackets-frame.width", "brackets-frame", "width", width)
    doc.bind("brackets-frame.height", "brackets-frame", "height", height)
    doc.bind("brackets-label.width", "brackets-label", "width", op("+", width, u(100)))
    doc.show("brackets", S("oq4.brackets"))
    fade = doc.timeline("brackets-fade", 800, [tr("brackets", "opacity", [(0, num(1)), (400, num(1)), (800, num(0))])],
                        complete="bracketsDone", essential=True)
    # A completed fade holds its endpoint after releasing the property, so a
    # new focus restores full alpha explicitly.
    shown = doc.timeline("brackets-show", 2, [tr("brackets", "opacity", [(0, num(1)), (2, num(1))])], essential=True)
    doc.on("showBrackets", cancel(fade, "base"), play(shown), {"op": "setState", "values": {"oq4.brackets": True}})
    doc.on("fadeBrackets", play(fade))
    doc.on("hideBrackets", cancel(fade, "base"), {"op": "setState", "values": {"oq4.brackets": False}})
    doc.on("bracketsDone", {"op": "setState", "values": {"oq4.brackets": False}})
    return group


# ============================================================ boss bar

def boss_bar(doc: HudDocument, *, offset: float = 0.0) -> dict:
    """The boss bar (14.12, B.5): a black 0.50 octagon with a tab, the health
    hexagon (#EA8F15 20% track, 35% fill, draining toward its leading point)
    between two C brackets of shield (#6A8C8A) that drain from the centre,
    #FF4000 pulsing at 2 Hz below 1000 shield points. Measured vertices."""
    doc.number("boss_health")
    doc.number("boss_maxhealth", 1)
    doc.number("boss_shield_percent")
    doc.boolean("oq4.boss")
    doc.boolean("oq4.bossShield")
    doc.boolean("oq4.bossWarn")
    plate = [(227.1, 55.9), (227.1, 71.0), (241.3, 85.3), (297.1, 85.3), (302.1, 89.2), (337.9, 89.2), (342.9, 85.3),
             (398.7, 85.3), (412.9, 71.0), (412.9, 55.9), (398.7, 41.6), (241.3, 41.6)]
    hexagon = [(246.0, 63.45), (255.2, 54.1), (382.8, 54.1), (392.0, 63.45), (382.8, 72.8), (255.2, 72.8)]
    left_bracket = [(231.0, 58.2), (231.3, 69.2), (245.2, 82.9), (303.9, 82.9), (310.5, 76.5), (252.3, 76.0), (239.9, 63.5),
                    (252.3, 50.9), (316.1, 50.9), (317.6, 49.2), (317.6, 45.6), (316.1, 44.0), (245.2, 44.0)]
    right_bracket = [(640 - x, y) for x, y in left_bracket]

    def poly(ident, pts, colour, **kw):
        return pts_path(ident, [(u(x + offset), u(y)) for x, y in pts], fill=ui.solid(colour), **kw)
    amber = "#EA8F15"
    teal = "#6A8C8A"
    nodes = [vec("boss-plate", [poly("plate", plate, [0, 0, 0, 0.5])]),
             vec("boss-track", [poly("track", hexagon, rgb(amber, 0.2))])]
    # Health: clipped from x 246 with width 149 health / (max + 1).
    health_fill = vec("boss-health-fill", [poly("fill", [(x + 1, y) for x, y in hexagon], rgb(amber, 0.35)),
                                           pts_path("bevel", [(u(x + 1 + offset), u(y)) for x, y in hexagon],
                                                    stroke=stroke(rgb(amber, 0.45), u(1.4), join="miter"))])
    health_window = box("boss-health-window", u(246 + offset), 0, u(149), u(100), [
        box("boss-health-content", u(-246 - offset), 0, u(640), u(100), [health_fill])], overflow=ui.keyword("hidden"))
    ratio = op("clamp", op("/", S("boss_health"), op("+", S("boss_maxhealth"), 1)), 0, 1)
    doc.bind("boss-health-window.width", "boss-health-window", "width", op("*", op("*", 149, ratio), U))
    nodes.append(health_window)
    # Shields: each half from its outer point toward the centre.
    shield = op("clamp", S("boss_shield_percent"), 0, 1)
    shields = []
    for side, pts, x0 in (("l", left_bracket, 230.0), ("r", right_bracket, 321.0)):
        track = vec(f"boss-shield-{side}-track", [poly("track", pts, rgb(teal, 0.2))])
        normal = vec(f"boss-shield-{side}-fill", [poly("fill", pts, rgb(teal, 0.35))])
        warn = vec(f"boss-shield-{side}-warn", [poly("warn", pts, rgb("#FF4000", 1))], {"opacity": num(1),
                                                                                       "display": ui.keyword("none")})
        doc.show(normal["id"], NOT(S("oq4.bossWarn")))
        doc.show(warn["id"], S("oq4.bossWarn"))
        content = box(f"boss-shield-{side}-content", u(-x0 - offset), 0, u(640), u(100), [normal, warn])
        window = box(f"boss-shield-{side}-window", u(x0 + offset), 0, u(89), u(100), [content], overflow=ui.keyword("hidden"))
        doc.bind(window["id"] + ".width", window["id"], "width", op("*", op("*", 89, shield), U))
        if side == "r":
            doc.bind(window["id"] + ".left", window["id"], "left", op("*", op("+", x0 + offset, op("*", 89, op("-", 1, shield))), U))
            doc.bind(content["id"] + ".left", content["id"], "left",
                     op("*", op("-", 0, op("+", x0 + offset, op("*", 89, op("-", 1, shield)))), U))
        shields += [track, window]
        doc.timeline(f"boss-warn-{side}", 500, [tr(warn["id"], "opacity", [(0, num(1)), (250, num(0.5)), (500, num(1))])],
                     iterations=0)
        doc.joins.append(play(f"boss-warn-{side}"))
    shield_group = full("boss-shields", shields, display=ui.keyword("none"))
    doc.show("boss-shields", S("oq4.bossShield"))
    nodes.append(shield_group)
    icon = vec("boss-icon", [svg_path("icon", "M320 76.4a3.6 3.6 0 1 0 .01 0zM319 78h2v1.4h1.4v2H321v1.4h-2v-1.4h-1.4v-2H319z",
                                      dx=u(offset), fill=ui.solid([1, 1, 1, 0.2]), rule="evenodd")])
    nodes.append(icon)
    group = full("boss", nodes, display=ui.keyword("none"))
    doc.show("boss", S("oq4.boss"))
    doc.on("showBossBar", {"op": "setState", "values": {"oq4.boss": True}})
    doc.on("hideBossBar", {"op": "setState", "values": {"oq4.boss": False}})
    doc.on("showBossShieldBar", {"op": "setState", "values": {"oq4.bossShield": True}})
    doc.on("hideBossShieldBar", {"op": "setState", "values": {"oq4.bossShield": False, "oq4.bossWarn": False}})
    doc.on("showBossShieldWarn", {"op": "setState", "values": {"oq4.bossWarn": True, "oq4.bossShield": True}})
    doc.on("hideBossShieldWarn", {"op": "setState", "values": {"oq4.bossWarn": False}})
    return group


# ============================================================ timed notices

ARROW5 = [(2.4, 1.5), (3.2, 16.2), (63.6, 31.0), (127.8, 15.1), (127.1, 1.2), (65.9, 16.4)]   # outer vertices, 130x33 u


def notices(doc: HudDocument) -> list:
    """EXIT under a chevron that rises 11 u with accel(500, 500) while fading
    from 0.50, for 5000 ms; "Game Saved..." for 2000 ms, fading over 1000 ms,
    drawn once (14.8). The missile warning pulses 70-30% at 2.5 Hz."""
    doc.boolean("oq4.exit")
    doc.boolean("oq4.saved")
    doc.boolean("oq4.missile")
    doc.number("missiledir")
    orange = rgb("#FF6600")
    exit_text = text_box("exit-text", "#str_200013", face="marine", size=28.8, colour=orange, baseline=u(58), center=u(324.5),
                         width=u(400))
    # arrow5 mirrored vertically: a double-stroke outline chevron pointing up.
    chevron_pts = [(u(261 + x), u(11 + 33 - y)) for x, y in ARROW5]
    chevron = vec("exit-chevron", [pts_path("chevron", chevron_pts, stroke=stroke(orange, u(3.4), join="miter"))],
                  {"transform": ui.transform(), "opacity": num(0.5)})
    accel = [0.455, 0.03, 0.515, 0.955]   # accel(500, 500): constant acceleration, then deceleration
    # Each cycle snaps back to rest on its last millisecond, where reduced
    # motion ends it: a static chevron at 0.50 (14.10).
    doc.timeline("exit-rise", 1000, [tr("exit-chevron", "transform", [(0, transform_value(0, 0), accel), (999, transform_value(0, u(-11))),
                                                                      (1000, transform_value(0, 0))]),
                                     tr("exit-chevron", "opacity", [(0, num(0.5)), (999, num(0)), (1000, num(0.5))])], iterations=0)
    exit_group = full("exit", [chevron, exit_text], display=ui.keyword("none"), opacity=num(1), transform=ui.transform())
    doc.show("exit", S("oq4.exit"))
    doc.timeline("exit-hold", 5000, [tr("exit", "opacity", [(0, num(1)), (5000, num(1))])], complete="exitDone", essential=True)
    doc.on("showExit", {"op": "setState", "values": {"oq4.exit": True}}, play("exit-rise"), play("exit-hold"))
    doc.on("hideExit", {"op": "setState", "values": {"oq4.exit": False}}, cancel("exit-hold"), cancel("exit-rise"))
    doc.on("exitDone", {"op": "setState", "values": {"oq4.exit": False}}, cancel("exit-rise"))

    saved = full("saved", shadowed("saved-text", "#str_200908", face="lowpixel", size=18, colour=[1, 1, 1, 1], baseline=u(80),
                                   center=u(320), width=u(600)), display=ui.keyword("none"), opacity=num(1), transform=ui.transform())
    doc.show("saved", S("oq4.saved"))
    doc.timeline("saved-hold", 3000, [tr("saved", "opacity", [(0, num(1)), (2000, num(1)), (3000, num(0))])], complete="savedDone",
                 essential=True)
    doc.on("saveMessage", {"op": "setState", "values": {"oq4.saved": True}}, play("saved-hold"))
    doc.on("savedDone", {"op": "setState", "values": {"oq4.saved": False}})

    red = rgb("#FF1A1A", 0.7)
    missile_text = text_box("missile-text", "#str_200891", face="marine", size=28.8, colour=red, baseline=u(88), center=u(320),
                            width=u(640), extra={"letter-spacing": ui.length(u(-2))})
    missile = full("missile", [missile_text], display=ui.keyword("none"), opacity=num(1), transform=ui.transform())
    doc.show("missile", S("oq4.missile"))
    doc.timeline("missile-pulse", 400, [tr("missile", "opacity", [(0, num(1)), (200, num(0.43)), (400, num(1))])], iterations=0)
    doc.on("missileThreatUp", {"op": "setState", "values": {"oq4.missile": True}}, play("missile-pulse"))
    doc.on("missileThreatDown", {"op": "setState", "values": {"oq4.missile": False}}, cancel("missile-pulse"))
    # Keep the authored single-notice placement, reserving rows below a boss
    # plate or earlier notice whenever several top-center groups coexist.
    # The boss ends at 89.2 u; an exit notice occupies 58 u plus its gutter.
    doc.boolean("oq4.boss")
    boss_offset = sel(S("oq4.boss"), u(92), 0)
    exit_offset = sel(S("oq4.exit"), u(64), 0)
    doc.bind("exit.stack", "exit", "transform", [0, boss_offset, 1, 1, 0])
    doc.bind("saved.stack", "saved", "transform", [0, op("+", sel(S("oq4.boss"), u(32), 0), exit_offset), 1, 1, 0])
    doc.bind("missile.stack", "missile", "transform",
             [0, op("+", op("+", sel(S("oq4.boss"), u(36), 0), exit_offset), sel(S("oq4.saved"), u(28), 0)), 1, 1, 0])
    return [exit_group, saved, missile]


def missile_chevron(doc: HudDocument) -> dict:
    """directional_hit2: a small chevron 88-114 u above the centre, rotated
    toward the threat, pulsing with the warning text."""
    red = rgb("#FF1A1A", 0.7)
    chevron = vec("missile-chevron", [pts_path("c", [(u(275), u(152)), (u(320), u(126)), (u(365), u(152)), (u(320), u(140))],
                                               fill=ui.solid(red), blend="additive")])
    turning = full("missile-dir", [chevron], transform=ui.transform(), display=ui.keyword("none"), opacity=num(1))
    doc.bind("missile-dir.rotation", "missile-dir", "transform", [0, 0, 1, 1, op("-", 0, S("missiledir"))])
    doc.show("missile-dir", S("oq4.missile"))
    doc.timeline("missile-dir-pulse", 400, [tr("missile-dir", "opacity", [(0, num(1)), (200, num(0.43)), (400, num(1))])],
                 iterations=0)
    doc.on("missileThreatUp", play("missile-dir-pulse"))
    return turning


# ======================================================= multiplayer extras

# Powerup emblems by icon base name, with the glow color the atlas gives each.
POWERUPS = {"item_quaddamage": "#00E3F1", "item_haste": "#B7E500", "item_regen": "#FF0000", "item_invis": "#8080FF",
            "item_guard": "#0074FF", "item_doubler": "#FF0000", "item_ammoregen": "#FFFF00", "item_scout": "#00FF00",
            "item_deadzone": "#FFFFFF"}
RING_TICKS = 30


def powerups(doc: HudDocument) -> dict:
    """Multiplayer powerups and the carried flag (14.9), bottom trailing: per
    powerup a dark disc with its emblem and glow, its countdown as a ring of
    ticks in the emblem's color, and the seconds left; the carried enemy flag
    above them. Each powerup keeps its own place in the column (the game's
    slots name them by icon), and the column hides while statistics are held."""
    doc.boolean("mp")
    doc.boolean("oq4.teamStrogg")
    doc.number("stat_visible")
    doc.number("powerup_flag_visible")
    for index in range(5):
        doc.number(f"powerup{index}_visible")
        doc.string(f"powerup{index}_icon")
        doc.string(f"powerup{index}_time")
        doc.number(f"oq4_powerup{index}_left", 1)
    slots = []
    for name, glow in POWERUPS.items():
        holds = [AND(op("!=", S(f"powerup{k}_visible"), 0), op("==", S(f"powerup{k}_icon"), name)) for k in range(5)]
        left, seconds_key = 1, BOUND_TEXT
        for k in reversed(range(5)):
            left = sel(holds[k], S(f"oq4_powerup{k}_left"), left)
            seconds_key = sel(holds[k], S(f"powerup{k}_time"), seconds_key)
        sid = "pu-" + name.replace("item_", "")
        cx, cy = 20, 13
        disc = vec(sid + "-disc", [pts_path("d", [(u(cx + 13 * math.cos(a * math.pi / 24)), u(cy + 13 * math.sin(a * math.pi / 24)))
                                                  for a in range(48)], fill=ui.solid([0, 0, 0, 0.5]))])
        ticks = []
        for k in range(RING_TICKS):
            angle = -90 + (k + 0.5) * 360 / RING_TICKS
            tx = cx + 13 * math.cos(math.radians(angle)) - 0.8
            ty = cy + 13 * math.sin(math.radians(angle)) - 1.3
            tick = ui.group(f"{sid}-t{k}", {**ui.absolute(left=u(tx), top=u(ty), width=u(1.6), height=u(2.6)), "display": ui.keyword("none"),
                                           "background-color": ui.colour(rgb(glow, 0.95)),
                                           "transform": transform_value(0, 0, 1, 1, angle)})
            doc.show(tick["id"], op("<", k, op("ceil", op("*", left, RING_TICKS))))
            ticks.append(tick)
        emblem = full(sid + "-emblem", hud_svg.icon_nodes(name, sid + "-g", u(cx), u(cy - 10), u(20), anchor="middle", glow=True))
        seconds = text_box(sid + "-sec", "", face="chain", size=u(11), colour=[1, 1, 1, 1], baseline=u(cy + 4), right=u(cx - 18),
                           width=u(40))
        doc.bind(sid + "-sec.text", seconds["id"], "text", seconds_key)
        slot = ui.group(sid, {"position": ui.keyword("relative"), "display": ui.keyword("none"), "width": ui.length(u(40)),
                              "height": ui.length(u(34)), "flex-shrink": ui.number(0)}, [disc, *ticks, emblem, seconds])
        doc.show(sid, OR(*holds))
        slots.append(slot)
    # The carried flag: the enemy's color (a Marine carries the Strogg flag).
    flags = []
    for team, colour in (("m", "#FF7B04"), ("s", "#6AA42B")):
        node = vec(f"pu-flag-{team}", [svg_path("c", "M3 2C7 0 10 4 15 2V10C10 12 7 8 3 10Z",
                                                matrix=(U, 0, -0.22 * U, U, u(1.8 + 12), u(4)),
                                                fill=ui.solid(rgb(colour, 0.45)),
                                                stroke={"paint": ui.solid(rgb(colour)), "widthDp": u(1.3), "minimumPixels": 0,
                                                        "join": "round", "cap": "butt"}),
                                       svg_path("s", "M3 1V17", matrix=(U, 0, -0.22 * U, U, u(1.8 + 12), u(4)),
                                                stroke={"paint": ui.solid(rgb(colour)), "widthDp": u(1.3), "minimumPixels": 0,
                                                        "join": "round", "cap": "butt"})], {"display": ui.keyword("none")})
        doc.show(node["id"], S("oq4.teamStrogg") if team == "s" else NOT(S("oq4.teamStrogg")))
        flags.append(node)
    flag_disc = vec("pu-flag-disc", [pts_path("d", [(u(20 + 13 * math.cos(a * math.pi / 24)), u(13 + 13 * math.sin(a * math.pi / 24)))
                                                    for a in range(48)], fill=ui.solid([0, 0, 0, 0.5]))])
    # The stock beat about the disc's center: 22 to 28 u over 150 ms and back
    # over 500 ms; reduced motion ends it at rest, so the wrapper holds 28 u.
    grow = 28 / 22
    beat = box("pu-flag-beat", 0, 0, u(40), u(26), flags, transform=ui.transform())
    peak = sel(S("oq4.reduced"), grow, 1)
    held = box("pu-flag-held", 0, 0, u(40), u(26), [beat], transform=ui.transform())
    doc.bind("pu-flag-held.reduced", "pu-flag-held", "transform", [0, 0, peak, peak, 0])
    doc.timeline("pu-flag-beat", 650, [tr("pu-flag-beat", "transform", [(0, transform_value()), (150, transform_value(sx=grow, sy=grow)),
                                                                        (650, transform_value())])], iterations=0)
    # The game sends oq4_carriedFlag when the flag comes or goes.
    step = when(op("!=", S("powerup_flag_visible"), 0), [play("pu-flag-beat")], [cancel("pu-flag-beat")])
    doc.on("oq4_carriedFlag", step)
    doc.joins.append(step)
    flag = ui.group("pu-flag", {"position": ui.keyword("relative"), "display": ui.keyword("none"), "width": ui.length(u(40)),
                                "height": ui.length(u(34)), "flex-shrink": ui.number(0)}, [flag_disc, held])
    doc.show("pu-flag", op("!=", S("powerup_flag_visible"), 0))
    column = ui.group("powerups", {**ui.absolute(left=u(592), top=u(100), width=u(40), height=u(313)), "display": ui.keyword("none"),
                                   "flex-direction": ui.keyword("column-reverse"), "align-items": ui.keyword("flex-end")},
                      slots + [flag])
    doc.bind("powerups.display", "powerups", "display", sel(AND(S("mp"), op("==", S("stat_visible"), 0)), "flex", "none"))
    return column


def dev_readouts(doc: HudDocument) -> list:
    """The speed readout (hud_showSpeed, top trailing under the kill feed) and
    the developer view comments (top leading), as the stock shows them."""
    doc.string("player_speed")
    doc.string("viewcomments")
    doc.boolean("oq4.speed")
    doc.boolean("oq4.comments")
    speed = text_box("speed", "", face="lowpixel", size=18, colour=[1, 1, 1, 0.6], baseline=u(86), right=u(630), width=u(200))
    doc.bind("speed.text", "speed", "text", S("player_speed"))
    speed["properties"]["display"] = ui.keyword("none")
    doc.show("speed", S("oq4.speed"))
    doc.on("showSpeed", {"op": "setState", "values": {"oq4.speed": True}})
    doc.on("hideSpeed", {"op": "setState", "values": {"oq4.speed": False}})
    comments = ui.label("comments", BOUND_TEXT, {**ui.typeface("profont", 13, 16, [1, 1, 1, 0.9]), **ui.absolute(left=u(2), top=u(5), width=u(636)),
                                                 "white-space": ui.keyword("pre-wrap"), "display": ui.keyword("none")})
    doc.bind("comments.text", "comments", "text", S("viewcomments"))
    doc.show("comments", S("oq4.comments"))
    doc.on("showViewComments", {"op": "setState", "values": {"oq4.comments": True}})
    doc.on("hideViewComments", {"op": "setState", "values": {"oq4.comments": False}})
    return [speed, comments]
