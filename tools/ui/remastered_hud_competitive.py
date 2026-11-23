"""The Competitive HUD preset (ui_hudLayout 3; ui-visual-design.md 13.9, 14.9).

In team modes, Duel and Tourney, Competitive moves the team panels and the
timer into one scoreline at the top center: the timer between two score slabs,
and further out each side's roster, up to four players as tiles with the weapon
in hand, health and armor bars where this viewer may see them, and a skull for
the dead. Network quality follows its trailing end at all times. Deathmatch
keeps the Remastered standing and timer top-leading, with network quality
under the timer. Competitive lists your team's players at the leading edge and
enlarges the status bar by 15% about the bottom edge. The kill feed, the center
column, the powerups and the managed-match card keep their places; the vote
takes the top-leading corner the panels left and the Tourney strip leaves the
top. The rosters replace the side team lists, which were clutter beside them. Constructions are the design atlas's (multiplayer
sheet); coordinates are the 640x480 u anchor canvases. The game publishes the
sides (oq4_mb*), the rosters (oq4_sl*), the line under the timer, the team
status rows (oq4_ts*) and the item timers (oq4_it_*).
"""
from __future__ import annotations

import build_retained_screens as ui
import hud_svg
from remastered_hud_base import (AND, NOT, OR, S, HudDocument, cancel, face_metrics, fitted_plate_path, num, op, play, pts_path, rgb,
                                 sel, stroke, tr, u, vec, when)
from remastered_hud_mp import (BAND, ERR, FEED_COLOURS, MAR, MAR_T, NAME, OK, STR, STR_T, VALUE, WHITE, bound_label, colour_of,
                               declare, ellipsis, flag_paths, flex_row, label, marker_vec, net_level, plate_vec, scoreline_shown,
                               text_at, under_match_bar)
from remastered_hud_status import MP_ROSTER, full

# The scoreline: sheared plates from y 4, 30 u tall and 2 u apart, a 64 u
# timer at 0.72 between two 56 u score slabs; 3 u further out each side's
# roster, four 20 u tiles on a 22 u pitch, each a 20 u plate over two vitals
# bars that end with the slabs.
BAR_Y, BAR_H, TIMER_W, SCORE_W = 4.0, 30.0, 64.0, 56.0
SCORE_X = (320 - TIMER_W / 2 - 2 - SCORE_W, 320 + TIMER_W / 2 + 2)
TILE_W, TILE_H, TILE_PITCH, ROSTER_GAP, ROSTER = 20.0, 20.0, 22.0, 3.0, 4
VITAL_Y, VITAL_H, VITAL_PITCH = BAR_Y + TILE_H + 2.2, 2.4, 3.4
# The ammunition bar over each tile, in the 4 u above the scoreline.
AMMO_Y, AMMO_H = 1.0, 1.8
# Where a roster's players stand: names cut at 66 u.
PLACE_W = 66.0
BAR_END = SCORE_X[1] + SCORE_W + ROSTER_GAP + ROSTER * TILE_PITCH - (TILE_PITCH - TILE_W)
UNDER_Y = BAR_Y + BAR_H + 2
TEAM_NAMES = ("#str_200197", "#str_200199")
# The client options every layout shows (MP-15): item timers on 13 u plates
# trailing at 633 u from y 96 on a 15 u pitch, the time of day above them, and
# the ammunition 13 u right of the crosshair.
ITEM_ROWS, ITEM_W, ITEM_ROW, ITEM_PITCH, ITEM_Y, ITEM_RIGHT = 6, 118.0, 13.0, 15.0, 96.0, 633.0
AT_BASE, TAKEN, DROPPED = "#str_231251", "#str_231252", "#str_231253"
# The item timers' kinds (rvItemTimers: quad, haste, regeneration,
# invisibility, mega health, heavy and light armor): the atlas symbol, its tint
# (the powerups keep their own colors) and the progress color.
ITEM_KINDS = ((1, "item_quaddamage", None, "#00E3F1"), (2, "item_haste", None, "#B7E500"), (3, "item_regen", None, "#FF4A3D"),
              (4, "item_invis", None, "#7D8CFF"), (5, "item_health", "#F7C004", "#F7C004"), (6, "item_armor", "#FA2B05", "#FA2B05"),
              (7, "item_armor", "#FFBE23", "#FFBE23"))


def competitive() -> dict:
    return op("==", S("hud.layout"), 3)


def team_mode() -> dict:
    return AND(S("oq4_team_game"), op("!=", S("gametype"), 2))


def plate_at(ident: str, x: float, y: float, w: float, h: float, alpha: float, colour: str = "#000000", **props) -> dict:
    """A sheared HUD plate of a fixed size at (x, y) u."""
    return ui.group(ident, {**ui.absolute(left=u(x), top=u(y), width=u(w), height=u(h)), "display": ui.keyword("block"), **props},
                    [plate_vec(ident + "-p", h, alpha, colour)])


def sheared_rect(ident: str, x: float, y: float, w: float, h: float, colour: list, cy: float, **props) -> dict:
    """A plain sheared band at (x, y) u inside a plate whose shear centers on
    cy u, leaning with the plates (0.22)."""
    pts = [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]
    return vec(ident, [pts_path("r", [(u(px - 0.22 * (py - cy)), u(py)) for px, py in pts], fill=ui.solid(colour))], props or None)


def side_team(side: int) -> dict:
    """The team on a side of the scoreline: yours leads, the Marines for
    someone who chose to watch (oq4_sl_left)."""
    left = S("oq4_sl_left")
    return left if side == 0 else op("-", 1, left)


# ------------------------------------------------------------------ the scoreline

def score_side(doc: HudDocument, side: int) -> list:
    """One side's score slab: the plate at 0.62, in team modes the team color
    at 0.85 over it with the faction mark at 0.22 behind the score in Chain
    21 u and the team's name under it in Lowpixel at 0.85. In Duel and Tourney
    the score in Chain 24 u fills the slab over a 2.5 u lip along its foot,
    #FFBE23 on the leader's side, and the player's name hangs under it on its
    own plate, growing outward from the inner end: #E4EBD2, white for you with
    the marker."""
    x = SCORE_X[side]
    team = side_team(side)
    teams = team_mode()
    prefix = f"oq4_mb{side}"
    ident = f"mb{side}"
    nodes = [plate_at(ident + "-plate", x, BAR_Y, SCORE_W, BAR_H, 0.62)]
    for t, colour in enumerate((MAR, STR)):
        block = plate_at(f"{ident}-block{t}", x, BAR_Y, SCORE_W, BAR_H, 0.85, colour, display=ui.keyword("none"))
        doc.show(block["id"], AND(teams, op("==", team, t)))
        logo = ui.group(f"{ident}-logo{t}", {**ui.absolute(left=u(x + SCORE_W / 2 - 13), top=u(BAR_Y + 2), width=u(26), height=u(26)),
                                             "display": ui.keyword("none"), "opacity": num(0.22)},
                        hud_svg.icon_nodes("marinelogo" if t == 0 else "strogglogo", f"{ident}-logo{t}-g", 0, 0, u(26), tint=WHITE))
        doc.show(logo["id"], AND(teams, op("==", team, t)))
        nodes += [block, logo]
    lead = AND(NOT(teams), S(prefix + "_lead"))
    lip = sheared_rect(ident + "-lip", 2.5, BAR_H - 3.5, SCORE_W - 5, 2.5, rgb(VALUE, 0.9), BAR_H / 2,
                       **{**ui.absolute(left=u(x), top=u(BAR_Y), width=u(SCORE_W), height=u(BAR_H)), "display": ui.keyword("none")})
    doc.show(lip["id"], lead)
    rest = sheared_rect(ident + "-rest", 2.5, BAR_H - 3.5, SCORE_W - 5, 2.5, [1, 1, 1, 0.22], BAR_H / 2,
                        **{**ui.absolute(left=u(x), top=u(BAR_Y), width=u(SCORE_W), height=u(BAR_H)), "display": ui.keyword("none")})
    doc.show(rest["id"], AND(NOT(teams), NOT(S(prefix + "_lead"))))
    nodes += [lip, rest]
    team_score = sel(op("==", team, 0), S("marine_score"), S("strogg_score"))
    score = text_at(doc, ident + "-score", op("numberText", team_score), 21, WHITE, center=x + SCORE_W / 2, baseline=BAR_Y + 19.2,
                    width=SCORE_W, face="chain", extra={"display": ui.keyword("none")})
    doc.show(score["id"], teams)
    duel_score = text_at(doc, ident + "-dscore", op("numberText", S(prefix + "_score")), 24, WHITE, center=x + SCORE_W / 2,
                         baseline=BAR_Y + 21.4, width=SCORE_W, face="chain", extra={"display": ui.keyword("none")})
    doc.show(duel_score["id"], NOT(teams))
    nodes += [score, duel_score]
    team_name = text_at(doc, ident + "-team", sel(op("==", team, 0), TEAM_NAMES[0], TEAM_NAMES[1]), 5.6, WHITE,
                        center=x + SCORE_W / 2, baseline=BAR_Y + 25.6, width=SCORE_W,
                        extra={"letter-spacing": ui.length(u(0.6)), "opacity": num(0.85), "display": ui.keyword("none")})
    doc.show(team_name["id"], teams)
    # Duel and Tourney: the name on a 13 u plate under the slab, 6 u in from
    # the inner end like the flags, cut at 110 u.
    you = S(prefix + "_you")
    name = bound_label(doc, ident + "-name", S(prefix + "_name"), 9.6, rgb(NAME), line=13, **ellipsis(110))
    doc.bind(ident + "-name.color", ident + "-name", "color", colour_of([(you, WHITE)], rgb(NAME)))
    mark = marker_vec(ident + "-marker", -6 if side == 0 else 0, 4)
    mark["properties"]["display"] = ui.keyword("none")
    doc.show(mark["id"], you)
    if side == 1:
        mark["properties"].update(ui.absolute(left=ui.length(100, "%"), top=0, width=u(6), height=u(13)))
    strip = flex_row(ident + "-strip", [plate_vec(ident + "-strip-p", 13, 0.5), name],
                     **{"height": ui.length(u(13)), "padding-left": ui.length(u(6)), "padding-right": ui.length(u(7))})
    place = {"right": u(640 - (x + SCORE_W - 6))} if side == 0 else {"left": u(x + 6)}
    holder = ui.group(ident + "-who", {**ui.absolute(top=u(UNDER_Y), height=u(13), **place), "display": ui.keyword("none")},
                      [strip, mark])
    doc.bind(ident + "-who.display", ident + "-who", "display", sel(AND(NOT(teams), op("!=", S(prefix + "_name"), "")), "block", "none"))
    return nodes + [team_name, holder]


def tile_x(side: int, i: int) -> float:
    """A roster tile's leading edge: tile 0 sits next to the slab."""
    if side == 0:
        return SCORE_X[0] - ROSTER_GAP - TILE_W - i * TILE_PITCH
    return SCORE_X[1] + SCORE_W + ROSTER_GAP + i * TILE_PITCH


def vital_bar(doc: HudDocument, ident: str, y: float, value, colours: list, default: list) -> dict:
    """A track at white 0.18 the tile's width, filled to the value of 100."""
    fill = ui.group(ident + "-fill", {"position": ui.keyword("absolute"), "left": ui.length(0), "top": ui.length(0),
                                      "width": ui.length(0, "%"), "height": ui.length(100, "%"), "display": ui.keyword("block"),
                                      "background-color": ui.colour(default)})
    doc.bind(ident + "-fill.width", fill["id"], "width", op("clamp", value, 0, 100))
    doc.bind(ident + "-fill.bg", fill["id"], "background-color", colour_of(colours, default))
    return ui.group(ident, {**ui.absolute(left=u(1), top=u(y), width=u(TILE_W - 2), height=u(VITAL_H)), "display": ui.keyword("block"),
                            "background-color": ui.colour([1, 1, 1, 0.18])}, [fill])


def roster(doc: HudDocument, side: int) -> list:
    """One side's roster, best score first from the slab outward: each tile a
    20 u plate at 0.55 with a 1.6 u lip in the team's color (the header band's
    in Duel and Tourney), lit in the team's text color at 0.28 for the player
    you follow and on the header-band plate for you. It names the player by
    the first two letters of their name in Lowpixel 10 u, in the team's text
    color (#E4EBD2 in Duel and Tourney), white for you and the player you
    follow; Quake 4 ships no portraits. Over the tile, where this viewer may
    see it, a 1.8 u bar fills to the ammunition for the weapon in hand in the
    weapon color code. Under it health and armor bars fill to 100: health white
    (#E46D56 at 25 and below, #FFBE23 above 100), armor #B5C784. The dead keep
    their initials at 0.35 under a skull in #E46D56 over a plate at 0.30."""
    declare(doc, {f"oq4_sl{side}_count": 0})
    team = side_team(side)
    teams = team_mode()
    nodes = []
    for i in range(ROSTER):
        p = f"oq4_sl{side}_{i}"
        declare(doc, {p + "_initials": "", p + "_open": False, p + "_weapon": -1, p + "_ammo": -1, p + "_health": 0,
                      p + "_armor": 0, p + "_dead": False, p + "_you": False, p + "_followed": False})
        rid = f"sl{side}t{i}"
        dead, opened, you, followed = S(p + "_dead"), S(p + "_open"), S(p + "_you"), S(p + "_followed")
        alive_plate = plate_at(rid + "-plate", 0, 0, TILE_W, TILE_H, 0.55)
        doc.show(alive_plate["id"], AND(NOT(dead), NOT(you)))
        dead_plate = plate_at(rid + "-deadp", 0, 0, TILE_W, TILE_H, 0.30, display=ui.keyword("none"))
        doc.show(dead_plate["id"], dead)
        band = plate_at(rid + "-band", 0, 0, TILE_W, TILE_H, 0.35, BAND, display=ui.keyword("none"))
        doc.show(band["id"], AND(you, NOT(dead)))
        # The lip takes the side's color; the light for the player you follow
        # the side's text color (white in Duel and Tourney).
        lip = ui.group(rid + "-lip", {**ui.absolute(left=u(2.2), top=u(0.6), width=u(TILE_W - 4.4), height=u(1.6)),
                                      "display": ui.keyword("block"), "background-color": ui.colour(rgb(BAND, 0.95))})
        doc.bind(rid + "-lip.bg", rid + "-lip", "background-color",
                 colour_of([(AND(teams, op("==", team, 0)), rgb(MAR, 0.95)), (teams, rgb(STR, 0.95))], rgb(BAND, 0.95)))
        lits = []
        for t, colour in enumerate((rgb(MAR_T, 0.28), rgb(STR_T, 0.28), [1, 1, 1, 0.22])):
            lit = vec(f"{rid}-lit{t}", [fitted_plate_path("l", u(TILE_H - 3), r=u(1), fill=ui.solid(colour), pad=0, top=u(1.5))],
                      {**ui.absolute(left=u(1), top=0, width=u(TILE_W - 2), height=u(TILE_H)), "display": ui.keyword("none")})
            doc.show(lit["id"], AND(followed, AND(teams, op("==", team, t)) if t < 2 else NOT(teams)))
            lits.append(lit)
        initials = text_at(doc, rid + "-ini", S(p + "_initials"), 10, rgb(MAR_T), center=TILE_W / 2, baseline=13.8, width=TILE_W,
                           extra={"letter-spacing": ui.length(u(0.3)), "opacity": num(1)})
        doc.bind(rid + "-ini.color", rid + "-ini", "color",
                 colour_of([(OR(you, followed), WHITE), (AND(teams, op("==", team, 0)), rgb(MAR_T)), (teams, rgb(STR_T))], rgb(NAME)))
        doc.bind(rid + "-ini.opacity", rid + "-ini", "opacity", sel(dead, 0.35, 1))
        skull = ui.group(rid + "-skull", {**ui.absolute(left=u(TILE_W / 2 - 6), top=u(4), width=u(12), height=u(12)),
                                          "display": ui.keyword("none")},
                         hud_svg.icon_nodes("skull", rid + "-skull-g", 0, 0, u(12), tint=rgb(ERR, 0.95)))
        doc.show(skull["id"], dead)
        # The ammunition for the weapon in hand, over the tile in the weapon's color.
        ammo = S(p + "_ammo")
        weapon = S(p + "_weapon")
        fill = ui.group(rid + "-ammo-fill", {"position": ui.keyword("absolute"), "left": ui.length(0), "top": ui.length(0),
                                             "width": ui.length(0, "%"), "height": ui.length(100, "%"), "display": ui.keyword("block"),
                                             "background-color": ui.colour([1, 1, 1, 0.9])})
        doc.bind(rid + "-ammo-fill.width", fill["id"], "width", op("clamp", ammo, 0, 100))
        doc.bind(rid + "-ammo-fill.bg", fill["id"], "background-color",
                 colour_of([(op("==", weapon, index), rgb(colour, 0.95)) for index, colour in enumerate(FEED_COLOURS)], [1, 1, 1, 0.9]))
        ammo_bar = ui.group(rid + "-ammo", {**ui.absolute(left=u(1), top=u(AMMO_Y - BAR_Y), width=u(TILE_W - 2), height=u(AMMO_H)),
                                            "display": ui.keyword("none"), "background-color": ui.colour([1, 1, 1, 0.15])}, [fill])
        doc.show(ammo_bar["id"], AND(opened, NOT(dead), op(">=", ammo, 0)))
        health = S(p + "_health")
        hp = vital_bar(doc, rid + "-hp", VITAL_Y - BAR_Y, health,
                       [(op("<=", health, 25), rgb(ERR, 0.9)), (op(">", health, 100), rgb(VALUE, 0.9))], [1, 1, 1, 0.9])
        armor = vital_bar(doc, rid + "-ar", VITAL_Y - BAR_Y + VITAL_PITCH, S(p + "_armor"), [], rgb(OK, 0.9))
        vitals = full(rid + "-vitals", [hp, armor], display=ui.keyword("none"))
        doc.show(vitals["id"], AND(opened, NOT(dead)))
        tile = ui.group(rid, {**ui.absolute(left=u(tile_x(side, i)), top=u(BAR_Y), width=u(TILE_W), height=u(BAR_H)),
                              "display": ui.keyword("none")},
                        [alive_plate, dead_plate, band, lip, *lits, initials, skull, ammo_bar, vitals])
        doc.show(rid, op(">", S(f"oq4_sl{side}_count"), i))
        nodes.append(tile)
    return nodes


def roster_places(doc: HudDocument, side: int) -> dict:
    """Where the players on a roster stand (hud_teamLocations): under the
    roster, one 7.5 u row a placed player on an 8.5 u pitch in tile order,
    each a plate at 0.45 with the initials in the team's text color (#E4EBD2 in
    Duel and Tourney) and the place in Lowpixel at 0.80, cut at 66 u, growing
    outward from the roster's inner end. It starts 2 u under the scoreline, or
    under the flags or the alive pips where they hang. The game names a place
    only for the tiles this viewer may look over, never their own."""
    team = side_team(side)
    teams = team_mode()
    gametype = S("gametype")
    hanging = OR(op("==", gametype, 4), op("==", gametype, 6), S("oq4_pips"))
    rows = []
    for i in range(ROSTER):
        p = f"oq4_sl{side}_{i}"
        declare(doc, {p + "_location": ""})
        rid = f"slp{side}r{i}"
        initials = bound_label(doc, rid + "-ini", S(p + "_initials"), 6, rgb(MAR_T), line=7.5,
                               **{"margin-right": ui.length(u(3)), "letter-spacing": ui.length(u(0.2))})
        doc.bind(rid + "-ini.color", rid + "-ini", "color",
                 colour_of([(AND(teams, op("==", team, 0)), rgb(MAR_T)), (teams, rgb(STR_T))], rgb(NAME)))
        place = bound_label(doc, rid + "-place", S(p + "_location"), 6.2, [1, 1, 1, 0.8], line=7.5, **ellipsis(PLACE_W))
        row = flex_row(rid, [plate_vec(rid + "-plate", 7.5, 0.45), initials, place],
                       **{"height": ui.length(u(7.5)), "padding-left": ui.length(u(4)), "padding-right": ui.length(u(5)),
                          "margin-bottom": ui.length(u(1)), "display": ui.keyword("none")})
        doc.bind(rid + ".display", rid, "display",
                 sel(AND(op(">", S(f"oq4_sl{side}_count"), i), op("!=", S(p + "_location"), "")), "flex", "none"))
        rows.append(row)
    edge = {"right": u(640 - (tile_x(0, 0) + TILE_W))} if side == 0 else {"left": u(tile_x(1, 0))}
    column = ui.group(f"slp{side}", {**ui.absolute(top=u(UNDER_Y), **edge), "display": ui.keyword("flex"),
                                     "flex-direction": ui.keyword("column"),
                                     "align-items": ui.keyword("flex-end" if side == 0 else "flex-start")}, rows)
    doc.bind(f"slp{side}.top", f"slp{side}", "top", sel(hanging, u(UNDER_Y + 15), u(UNDER_Y)))
    return column


def bar_timer(doc: HudDocument) -> list:
    """The timer plate at 0.72: the time in Chain 17 u with the Remastered
    timer's states (WARM-UP, the countdown in #FFBE23, #FFBE23 in the last
    minute, #E46D56 pulsing in the last ten seconds, the infinity mark in
    sudden death and review), and under it in Lowpixel 700 at 0.55 the limit,
    the round or the arena, the ready count, STARTS IN or SUDDEN DEATH."""
    declare(doc, {"oq4_mb_sub": "", "oq4_mb_ready": ""})
    state = S("oq4_state")
    countdown = S("oq4_countdown")
    warmup = AND(op("==", state, 1), NOT(countdown))
    sudden = AND(op("==", state, 4), NOT(countdown))
    review = op(">=", state, 5)
    live = AND(NOT(countdown), NOT(warmup), NOT(sudden), NOT(review))
    last_ten = AND(live, NOT(S("overtime")), op("==", S("oq4_clock_warn"), 2))
    last_minute = AND(live, NOT(S("overtime")), op(">=", S("oq4_clock_warn"), 1))
    untimed = AND(live, NOT(S("oq4_timed")))
    x = 320 - TIMER_W / 2
    nodes = [plate_at("mbt-plate", x, BAR_Y, TIMER_W, BAR_H, 0.72)]
    sub_text = sel(warmup, sel(S("showready"), S("oq4_mb_ready"), ""),
                   sel(AND(live, S("overtime")), S("overtimetext"), S("oq4_mb_sub")))
    has_sub = OR(countdown, sudden, op("!=", sub_text, ""))
    clock = text_at(doc, "mbt-clock", S("oq4_clock"), 17, WHITE, center=320, baseline=BAR_Y + 16.4, width=TIMER_W, face="chain",
                    extra={"display": ui.keyword("none")})
    doc.bind("mbt-clock.color", "mbt-clock", "color",
             colour_of([(countdown, rgb(VALUE)), (last_minute, rgb(VALUE)), (untimed, [1, 1, 1, 0.7])], WHITE))
    doc.show("mbt-clock", AND(OR(countdown, live), NOT(last_ten)))
    urgent = text_at(doc, "mbt-urgent", S("oq4_clock"), 17, rgb(ERR), center=320, baseline=BAR_Y + 16.4, width=TIMER_W, face="chain",
                     extra={"display": ui.keyword("none"), "opacity": num(1)})
    doc.show("mbt-urgent", last_ten)
    warm = text_at(doc, "mbt-warm", None, 10.4, WHITE, center=320, baseline=BAR_Y + 15.8, width=TIMER_W,
                   extra={"letter-spacing": ui.length(u(0.4)), "display": ui.keyword("none")})
    warm["properties"]["text"] = ui.text("#str_231206")
    doc.show("mbt-warm", warmup)
    infinity = ui.group("mbt-inf", {**ui.absolute(left=u(320 - 7.5), top=u(BAR_Y + 4.5), width=u(15), height=u(15)),
                                    "display": ui.keyword("none")},
                        hud_svg.icon_nodes("icon_infinity", "mbt-inf-g", 0, 0, u(15), tint=WHITE))
    doc.show("mbt-inf", OR(sudden, review))
    nodes += [clock, urgent, warm, infinity]
    # Without a line under it, the time sits 3 u lower, centred on the plate.
    for ident in ("mbt-clock", "mbt-urgent", "mbt-warm"):
        size = u(10.4 if ident == "mbt-warm" else 17)
        face = "lowpixel" if ident == "mbt-warm" else "chain"
        top = u(BAR_Y + (15.8 if ident == "mbt-warm" else 16.4)) - size * face_metrics(face)["ascent"]
        doc.bind(ident + ".top", ident, "top", sel(has_sub, top, top + u(3)))
    sub = text_at(doc, "mbt-sub", sub_text, 5.9, [1, 1, 1, 0.55], center=320, baseline=BAR_Y + 25.4, width=TIMER_W + 10,
                  extra={"letter-spacing": ui.length(u(0.55))})
    starts = text_at(doc, "mbt-starts", None, 5.9, [1, 1, 1, 0.55], center=320, baseline=BAR_Y + 25.4, width=TIMER_W + 10,
                     extra={"letter-spacing": ui.length(u(0.55)), "display": ui.keyword("none")})
    starts["properties"]["text"] = ui.text("#str_231207")
    sd = text_at(doc, "mbt-sd", None, 5.9, [1, 1, 1, 0.55], center=320, baseline=BAR_Y + 25.4, width=TIMER_W + 10,
                 extra={"letter-spacing": ui.length(u(0.55)), "display": ui.keyword("none")})
    sd["properties"]["text"] = ui.text("#str_231208")
    doc.show("mbt-sub", NOT(OR(countdown, sudden)))
    doc.show("mbt-starts", countdown)
    doc.show("mbt-sd", sudden)
    doc.timeline("mbt-pulse", 1000, [tr("mbt-urgent", "opacity", [(0, num(1)), (300, num(0.45), [0.0, 0.0, 0.58, 1.0]),
                                                                   (1000, num(1))])], iterations=0)
    step = when(op("==", S("oq4_clock_warn"), 2), [play("mbt-pulse")], [cancel("mbt-pulse")])
    doc.on("oq4_clockWarn", step)
    doc.joins.append(step)
    return nodes + [sub, starts, sd]


def under_bar(doc: HudDocument) -> list:
    """Under the bar, each side's state hangs from its inner end: its flag (AT
    BASE, TAKEN with the carrier, or DROPPED) or the round modes' alive pips;
    the Dead Zone holder sits under the timer."""
    gametype = S("gametype")
    two_flag = OR(op("==", gametype, 4), op("==", gametype, 6))
    nodes = []
    for side in (0, 1):
        team = side_team(side)
        # 6 u from the inner end: the leading side's plate grows toward the
        # leading edge with its words.
        place = {"right": u(640 - (SCORE_X[0] + SCORE_W - 6))} if side == 0 else {"left": u(SCORE_X[1] + 6)}
        ident = f"mbf{side}"
        state = sel(op("==", team, 0), S("oq4_flag0_state"), S("oq4_flag1_state"))
        icons = []
        for t, colour in enumerate((MAR, STR)):
            for kind, value in (("base", 0), ("taken", 1), ("dropped", 2)):
                icon = vec(f"{ident}-{t}{kind}", flag_paths(f"f{t}{kind}", rgb(colour), kind, 0.72),
                           {**ui.absolute(left=u(3), top=u(-1.5), width=u(14), height=u(14)), "display": ui.keyword("none")})
                doc.show(icon["id"], AND(op("==", team, t), op("==", state, value)))
                icons.append(icon)
        words = []
        for kind, key, value in (("base", AT_BASE, 0), ("taken", TAKEN, 1), ("dropped", DROPPED, 2)):
            word = label(f"{ident}-{kind}", key, 6.8, [1, 1, 1, 0.8], line=13,
                         **{"letter-spacing": ui.length(u(0.4)), "display": ui.keyword("none")})
            doc.show(word["id"], op("==", state, value))
            words.append(word)
        carrier = bound_label(doc, f"{ident}-carrier", sel(op("==", team, 0), S("oq4_flag0_carrier"), S("oq4_flag1_carrier")),
                              6.8, [1, 1, 1, 0.8], line=13, **{**ellipsis(40), "margin-left": ui.length(u(3)), "display": ui.keyword("none")})
        doc.show(carrier["id"], op("==", state, 1))
        row = flex_row(f"{ident}-row", [plate_vec(f"{ident}-plate", 13, 0.5), *words, carrier],
                       **{"height": ui.length(u(13)), "padding-left": ui.length(u(17)), "padding-right": ui.length(u(6))})
        holder = ui.group(ident, {**ui.absolute(top=u(UNDER_Y), height=u(13), **place), "display": ui.keyword("none")},
                          [row, *icons])
        doc.bind(ident + ".display", ident, "display", sel(two_flag, "block", "none"))
        nodes.append(holder)
        # Round modes: five alive pips on a 40x11 u plate.
        px = SCORE_X[0] + SCORE_W - 46 if side == 0 else SCORE_X[1] + 6
        pips = []
        alive = sel(op("==", team, 0), S("oq4_alive0"), S("oq4_alive1"))
        for k in range(5):
            lx = 5 + k * 6.6
            pts = [(lx, 2.5), (lx + 4.4, 2.5), (lx + 4.4, 8.5), (lx, 8.5)]
            pts = [(u(x0 - 0.22 * (y0 - 5.5)), u(y0)) for x0, y0 in pts]
            for t, text_colour in enumerate((MAR_T, STR_T)):
                lit = vec(f"mbp{side}-{t}-{k}", [pts_path("p", pts, fill=ui.solid(rgb(text_colour, 0.9)))], {"display": ui.keyword("none")})
                dim = vec(f"mbp{side}-{t}-{k}d", [pts_path("p", pts, fill=ui.solid(rgb(text_colour, 0.2)))], {"display": ui.keyword("none")})
                doc.show(lit["id"], AND(op("==", team, t), op("<", k, alive)))
                doc.show(dim["id"], AND(op("==", team, t), op(">=", k, alive)))
                pips += [lit, dim]
        pip_box = ui.group(f"mbp{side}", {**ui.absolute(left=u(px), top=u(UNDER_Y), width=u(40), height=u(11)), "display": ui.keyword("none")},
                           [plate_vec(f"mbp{side}-plate", 11, 0.5), *pips])
        doc.show(pip_box["id"], S("oq4_pips"))
        nodes.append(pip_box)
    # Dead Zone: the holder under the timer, CONTESTED or EMPTY.
    zone_rows = []
    for value, key, colour, text_colour in ((1, TEAM_NAMES[0], MAR, rgb(MAR_T)), (2, TEAM_NAMES[1], STR, rgb(STR_T)),
                                            (3, "#str_231219", VALUE, rgb(VALUE)), (0, "#str_231220", "#FFFFFF", [1, 1, 1, 0.55])):
        swatch = ui.group(f"mbz-sw{value}", {"position": ui.keyword("relative"), "display": ui.keyword("block"), "width": ui.length(u(7)),
                                              "height": ui.length(u(6)), "margin-right": ui.length(u(4)),
                                              "background-color": ui.colour(rgb(colour, 0.0 if value == 0 else 1.0))})
        row = flex_row(f"mbz-v{value}", [swatch, label(f"mbz-t{value}", key, 6.6, text_colour, line=12,
                                                       **{"letter-spacing": ui.length(u(0.5))})], display=ui.keyword("none"))
        doc.bind(f"mbz-v{value}.display", f"mbz-v{value}", "display", sel(op("==", S("oq4_zone"), value), "flex", "none"))
        zone_rows.append(row)
    zone = ui.group("mbz", {**ui.absolute(left=0, top=u(UNDER_Y), width=u(640), height=u(12)), "display": ui.keyword("none"),
                            "justify-content": ui.keyword("center")},
                    [flex_row("mbz-c", [plate_vec("mbz-plate", 12, 0.5), *zone_rows],
                              **{"height": ui.length(u(12)), "padding-left": ui.length(u(6)), "padding-right": ui.length(u(6))})])
    doc.bind("mbz.display", "mbz", "display", sel(op("==", gametype, 8), "flex", "none"))
    return nodes + [zone]


def net_quality(doc: HudDocument, ident: str = "mbn", place: dict | None = None) -> dict:
    """Network quality (13.13) at all times: the ping in Chain at 0.75 beside
    four rising bars, lit by the ping and the loss. It follows the
    scoreline's trailing end, or in deathmatch sits under the timer."""
    ping = S("oq4_ping")
    lit = net_level(doc)
    bars = []
    for k in range(4):
        h = 2 + k * 1.6
        bar = ui.group(f"{ident}-b{k}", {"position": ui.keyword("relative"), "display": ui.keyword("block"), "width": ui.length(u(2.2)),
                                         "height": ui.length(u(h)), "margin-left": ui.length(u(1.2)),
                                         "background-color": ui.colour([1, 1, 1, 0.2])})
        doc.bind(f"{ident}-b{k}.bg", bar["id"], "background-color",
                 colour_of([(AND(op("<", k, lit), op(">=", lit, 3)), rgb(OK, 0.9)), (AND(op("<", k, lit), op("==", lit, 2)), rgb("#E3AD36", 0.9)),
                            (op("<", k, lit), rgb(ERR, 0.9))], [1, 1, 1, 0.2]))
        bars.append(bar)
    if place is None:
        place = {"position": ui.keyword("absolute"), "left": ui.length(u(BAR_END + 6)), "top": ui.length(u(BAR_Y + 10)),
                 "height": ui.length(u(10))}
    row = flex_row(ident, [bound_label(doc, f"{ident}-ping", op("numberText", ping), 8, [1, 1, 1, 0.75], face="chain", line=10),
                           label(f"{ident}-ms", "#str_231228", 6, [1, 1, 1, 0.6], line=10, **{"margin-left": ui.length(u(1.5)),
                                                                                              "margin-right": ui.length(u(1))}),
                           flex_row(f"{ident}-bars", bars, **{"align-items": ui.keyword("flex-end"), "height": ui.length(u(7))})],
                   **place)
    return row


def match_bar(doc: HudDocument) -> dict:
    declare(doc, {"marine_score": 0, "strogg_score": 0, "oq4_my_team": 0, "oq4_sl_left": 0, "oq4_team_game": False, "oq4_ping": 0,
                  "oq4_zone": 0, "oq4_pips": False, "oq4_state": 0, "oq4_countdown": False, "oq4_timed": False, "oq4_clock_warn": 0,
                  "oq4_clock": "", "showready": False, "overtime": False, "overtimetext": ""})
    for side in (0, 1):
        declare(doc, {f"oq4_mb{side}_name": "", f"oq4_mb{side}_score": 0, f"oq4_mb{side}_you": False,
                      f"oq4_mb{side}_lead": False})
    for flag in range(3):
        declare(doc, {f"oq4_flag{flag}_state": 0, f"oq4_flag{flag}_carrier": ""})
    for team in range(2):
        declare(doc, {f"oq4_alive{team}": 0})
    nodes = (roster(doc, 0) + roster(doc, 1) + score_side(doc, 0) + score_side(doc, 1) + bar_timer(doc) + under_bar(doc) +
             [roster_places(doc, 0), roster_places(doc, 1), net_quality(doc)])
    group = full("matchbar", nodes, display=ui.keyword("none"))
    doc.show("matchbar", scoreline_shown())
    return group


def deathmatch_net(doc: HudDocument) -> dict:
    """Deathmatch's network quality in Competitive: under the Remastered timer
    in its column, 3 u below."""
    row = net_quality(doc, "ffn", {"position": ui.keyword("relative"), "height": ui.length(u(10)), "margin-top": ui.length(u(3)),
                                   "margin-left": ui.length(u(4)), "display": ui.keyword("none")})
    doc.bind("ffn.display", "ffn", "display", sel(AND(competitive(), NOT(scoreline_shown())), "flex", "none"))
    return row


# ---------------------------------------------------------------- client options

def crosshair_ammo(doc: HudDocument) -> dict:
    """The ammunition of the weapon in hand 13 u right of the crosshair
    (hud_crosshairAmmo), in Chain at 0.80, #E46D56 when the weapon is low."""
    declare(doc, {"oq4_xhair_ammo": "", "oq4_xhair_ammo_low": False})
    node = text_at(doc, "xammo", S("oq4_xhair_ammo"), 9.6, [1, 1, 1, 0.8], x=333, baseline=244.6, face="chain",
                   extra={"display": ui.keyword("none")})
    doc.bind("xammo.color", "xammo", "color", colour_of([(S("oq4_xhair_ammo_low"), rgb(ERR, 0.9))], [1, 1, 1, 0.8]))
    doc.show("xammo", op("!=", S("oq4_xhair_ammo"), ""))
    return node


def item_timers_drop() -> dict:
    """How far the item timers step down: under the scoreline with the kill
    feed on narrow views (UNDER_BAR_DROP)."""
    from remastered_hud_mp import UNDER_BAR_DROP
    return sel(under_match_bar(), u(UNDER_BAR_DROP), 0)


def item_timers(doc: HudDocument) -> dict:
    """The item timers the server admits this player to (si_itemTimers,
    hud_itemTimers), spectators by default: up to six 118x13 u rows trailing
    at 633 u from y 96 on a 15 u pitch, each a plate at 0.45 with the item's
    symbol (the powerups in their own colors, mega health #F7C004, heavy armor
    #FA2B05, light armor #FFBE23), where it lies (its name where the map names
    no place) and the time left in Chain #FFBE23 over a 1 u line filling in
    the item's color as the respawn runs. An item that is there reads in
    #B5C784 at full strength with no time; one counting down dims its symbol
    to 0.45; a held item reads --. The time of day (hud_localClock) sits above
    them at 0.60."""
    declare(doc, {"oq4_it_count": 0, "oq4_local_clock": ""})
    clock = text_at(doc, "localclock", S("oq4_local_clock"), 7.4, [1, 1, 1, 0.6], baseline=ITEM_Y - 4, face="chain",
                    right=ITEM_RIGHT, extra={"display": ui.keyword("none"), "max-width": ui.length(u(ITEM_W))})
    doc.show("localclock", op("!=", S("oq4_local_clock"), ""))
    rows = [clock]
    x = ITEM_RIGHT - ITEM_W
    for i in range(ITEM_ROWS):
        p = f"oq4_it_{i}"
        declare(doc, {p + "_name": "", p + "_time": "", p + "_up": False, p + "_where": "", p + "_kind": 0, p + "_left": 0})
        rid = f"itimer{i}"
        up, kind = S(p + "_up"), S(p + "_kind")
        plate = plate_at(rid + "-plate", 0, 0, ITEM_W, ITEM_ROW, 0.45)
        icons = []
        for value, glyph, tint, _ in ITEM_KINDS:
            holder = ui.group(f"{rid}-k{value}", {**ui.absolute(left=0, top=0, width=u(10), height=u(10)), "display": ui.keyword("none")},
                              hud_svg.icon_nodes(glyph, f"{rid}-k{value}-g", 0, 0, u(10), tint=rgb(tint) if tint else None))
            doc.show(holder["id"], op("==", kind, value))
            icons.append(holder)
        symbol = ui.group(rid + "-sym", {**ui.absolute(left=u(4), top=u(1.5), width=u(10), height=u(10)), "display": ui.keyword("block"),
                                         "opacity": num(1)}, icons)
        doc.bind(rid + "-sym.opacity", rid + "-sym", "opacity", sel(up, 1, 0.45))
        # A fixed box: the place is cut only when it would reach the time.
        place = text_at(doc, rid + "-name", sel(op("!=", S(p + "_where"), ""), S(p + "_where"), S(p + "_name")), 6.6, [1, 1, 1, 0.8],
                        x=17, baseline=8.4, extra={**ellipsis(ITEM_W - 50), "width": ui.length(u(ITEM_W - 50))})
        doc.bind(rid + "-name.color", rid + "-name", "color", colour_of([(up, rgb(OK))], [1, 1, 1, 0.8]))
        time = text_at(doc, rid + "-time", S(p + "_time"), 8.4, rgb(VALUE), baseline=8.8, face="chain",
                       right=ITEM_W - 5, extra={"max-width": ui.length(u(28))})
        fill = ui.group(rid + "-fill", {"position": ui.keyword("absolute"), "left": ui.length(0), "top": ui.length(0),
                                         "width": ui.length(0, "%"), "height": ui.length(100, "%"), "display": ui.keyword("block"),
                                         "background-color": ui.colour(rgb(VALUE, 0.9))})
        doc.bind(rid + "-fill.width", fill["id"], "width", op("*", 100, op("-", 1, op("clamp", S(p + "_left"), 0, 1))))
        doc.bind(rid + "-fill.bg", fill["id"], "background-color",
                 colour_of([(op("==", kind, value), rgb(progress, 0.9)) for value, _, _, progress in ITEM_KINDS], rgb(VALUE, 0.9)))
        track = ui.group(rid + "-track", {**ui.absolute(left=u(17), top=u(ITEM_ROW - 2.2), width=u(ITEM_W - 24), height=u(1)),
                                          "display": ui.keyword("none"), "background-color": ui.colour([1, 1, 1, 0.15])}, [fill])
        doc.show(track["id"], AND(NOT(up), op("!=", S(p + "_time"), ""), op("!=", S(p + "_time"), "--")))
        row = ui.group(rid, {**ui.absolute(left=u(x), top=u(ITEM_Y + i * ITEM_PITCH), width=u(ITEM_W), height=u(ITEM_ROW)),
                             "display": ui.keyword("none")}, [plate, symbol, place, time, track])
        doc.show(rid, op(">", S("oq4_it_count"), i))
        rows.append(row)
    group = full("itimers", rows)
    doc.bind("itimers.top", "itimers", "top", item_timers_drop())
    return group
