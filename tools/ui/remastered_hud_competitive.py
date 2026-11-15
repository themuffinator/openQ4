"""The Competitive HUD preset (ui_hudLayout 3; ui-visual-design.md 13.9, 14.9).

In multiplayer, Competitive moves the standing or team panels and the timer
into one match bar at the top center (your side, the timer, the other side),
keeps network quality beside it at all times, lists your team's players at
the leading edge and enlarges the status bar by 15% about the bottom edge.
The kill feed, the center column, the powerups and the managed-match card keep
their places; the vote takes the top-leading corner the standing left and the
Tourney strip leaves the top. Spectators see both teams' lists flanking the
view. Constructions are the design atlas's (multiplayer sheet); coordinates
are the 640x480 u anchor canvases. The game publishes the bar's sides
(oq4_mb*), the line under the timer and the team status rows (oq4_ts*).
"""
from __future__ import annotations

import build_retained_screens as ui
import hud_svg
from remastered_hud_base import (AND, NOT, OR, S, HudDocument, cancel, face_metrics, fitted_plate_path, num, op, play, pts_path, rgb,
                                 sel, stroke, tr, u, vec, when)
from remastered_hud_mp import (BAND, ERR, FEED_COLOURS, MAR, MAR_T, NAME, OK, STR, STR_T, VALUE, WHITE, bound_label, colour_of,
                               declare, ellipsis, flag_paths, flex_row, label, marker_vec, net_level, plate_vec, text_at)
from remastered_hud_status import MP_ROSTER, full

# The match bar (atlas matchBar): three sheared plates from y 6, 26 u tall and
# 2 u apart, a 60 u timer between two 122 u sides.
BAR_Y, BAR_H, SIDE_W, TIMER_W = 6.0, 26.0, 122.0, 60.0
SIDE_X = (320 - TIMER_W / 2 - 2 - SIDE_W, 320 + TIMER_W / 2 + 2)
UNDER_Y = BAR_Y + BAR_H + 2
# Team status (atlas teamList): 138x14.5 u plates on a 16.5 u pitch from y 150.
LIST_W, LIST_ROW, LIST_PITCH, LIST_Y, LIST_ROWS = 138.0, 14.5, 16.5, 150.0, 8
TEAM_NAMES = ("#str_200197", "#str_200199")
# A team mate's location beside their row, and the client options every layout
# shows (MP-15): item timers on 8.6 u plates trailing at 633 u from y 96, the
# time of day above them, and the ammunition 13 u right of the crosshair.
LOCATION_W = 78.0
ITEM_ROWS, ITEM_W, ITEM_ROW, ITEM_PITCH, ITEM_Y, ITEM_RIGHT = 6, 112.0, 8.6, 9.8, 96.0, 633.0
AT_BASE, TAKEN, DROPPED = "#str_231251", "#str_231252", "#str_231253"


def competitive() -> dict:
    return op("==", S("hud.layout"), 3)


def team_mode() -> dict:
    return AND(S("oq4_team_game"), op("!=", S("gametype"), 2))


def plate_at(ident: str, x: float, y: float, w: float, h: float, alpha: float, colour: str = "#000000", **props) -> dict:
    """A sheared HUD plate of a fixed size at (x, y) u."""
    return ui.group(ident, {**ui.absolute(left=u(x), top=u(y), width=u(w), height=u(h)), "display": ui.keyword("block"), **props},
                    [plate_vec(ident + "-p", h, alpha, colour)])


def side_team(side: int) -> dict:
    """The team on a side of the bar: yours leads."""
    mine = S("oq4_my_team")
    return mine if side == 0 else op("-", 1, mine)


# ------------------------------------------------------------------ the match bar

def bar_side(doc: HudDocument, side: int) -> list:
    """One side: the plate at 0.55, its outer 25 u block (the team color at 0.85
    with the faction mark, or the rank in deathmatch), the team or player name
    and the score in Chain 21 u at its inner end."""
    x = SIDE_X[side]
    outer = x if side == 0 else x + SIDE_W - 25
    team = side_team(side)
    teams = team_mode()
    prefix = f"oq4_mb{side}"
    ident = f"mb{side}"
    nodes = [plate_at(ident + "-plate", x, BAR_Y, SIDE_W, BAR_H, 0.55)]
    # The outer block: a team's color and mark, or the deathmatch band.
    for t, colour in enumerate((MAR, STR)):
        block = plate_at(f"{ident}-block{t}", outer, BAR_Y, 25, BAR_H, 0.85, colour, display=ui.keyword("none"))
        doc.show(block["id"], AND(teams, op("==", team, t)))
        logo = ui.group(f"{ident}-logo{t}", {**ui.absolute(left=u(outer + 4.5), top=u(BAR_Y + 5), width=u(16), height=u(16)),
                                             "display": ui.keyword("none")},
                        hud_svg.icon_nodes("marinelogo" if t == 0 else "strogglogo", f"{ident}-logo{t}-g", 0, 0, u(16), tint=WHITE))
        doc.show(logo["id"], AND(teams, op("==", team, t)))
        nodes += [block, logo]
    band = plate_at(ident + "-band", outer, BAR_Y, 25, BAR_H, 0.55, BAND, display=ui.keyword("none"))
    doc.show(band["id"], NOT(teams))
    rank = text_at(doc, ident + "-rank", S(prefix + "_rank"), 7.6, WHITE, center=outer + 12.5, baseline=BAR_Y + 16.5, width=25,
                   extra={"letter-spacing": ui.length(u(0.3)), "display": ui.keyword("none")})
    doc.bind(ident + "-rank.color", ident + "-rank", "color", colour_of([(S(prefix + "_lead"), rgb(VALUE))], WHITE))
    doc.show(rank["id"], NOT(teams))
    nodes += [band, rank]
    # The name: the team's in its text color, or the player's cut at 52 u.
    name_kw = {"x": x + 31} if side == 0 else {"right": x + SIDE_W - 31}
    team_name = text_at(doc, ident + "-team", sel(op("==", team, 0), TEAM_NAMES[0], TEAM_NAMES[1]), 7.4, rgb(MAR_T),
                        baseline=BAR_Y + 16.8, extra={"letter-spacing": ui.length(u(0.7)), "display": ui.keyword("none")}, **name_kw)
    doc.bind(ident + "-team.color", ident + "-team", "color", colour_of([(op("==", team, 0), rgb(MAR_T))], rgb(STR_T)))
    doc.show(team_name["id"], teams)
    player = text_at(doc, ident + "-name", S(prefix + "_name"), 10.6, rgb(NAME), baseline=BAR_Y + 16.8,
                     extra={**ellipsis(52), "display": ui.keyword("none")}, **name_kw)
    doc.bind(ident + "-name.color", ident + "-name", "color", colour_of([(S(prefix + "_you"), WHITE)], rgb(NAME)))
    doc.show(player["id"], NOT(teams))
    mark = marker_vec(ident + "-marker", x - 6 if side == 0 else x + SIDE_W + 1, BAR_Y + 10)
    mark["properties"]["display"] = ui.keyword("none")
    doc.show(mark["id"], AND(NOT(teams), S(prefix + "_you")))
    nodes += [team_name, player, mark]
    score = sel(teams, sel(op("==", team, 0), S("marine_score"), S("strogg_score")), S(prefix + "_score"))
    score_kw = {"right": x + SIDE_W - 7} if side == 0 else {"x": x + 7}
    nodes.append(text_at(doc, ident + "-score", op("numberText", score), 21, WHITE, baseline=BAR_Y + 20.6, face="chain", **score_kw))
    return nodes


def bar_timer(doc: HudDocument) -> list:
    """The timer plate at 0.72: the time in Chain 15.5 u with the Remastered
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
    clock = text_at(doc, "mbt-clock", S("oq4_clock"), 15.5, WHITE, center=320, baseline=BAR_Y + 14.6, width=TIMER_W, face="chain",
                    extra={"display": ui.keyword("none")})
    doc.bind("mbt-clock.color", "mbt-clock", "color",
             colour_of([(countdown, rgb(VALUE)), (last_minute, rgb(VALUE)), (untimed, [1, 1, 1, 0.7])], WHITE))
    doc.show("mbt-clock", AND(OR(countdown, live), NOT(last_ten)))
    urgent = text_at(doc, "mbt-urgent", S("oq4_clock"), 15.5, rgb(ERR), center=320, baseline=BAR_Y + 14.6, width=TIMER_W, face="chain",
                     extra={"display": ui.keyword("none"), "opacity": num(1)})
    doc.show("mbt-urgent", last_ten)
    warm = text_at(doc, "mbt-warm", None, 9.6, WHITE, center=320, baseline=BAR_Y + 14.6, width=TIMER_W,
                   extra={"letter-spacing": ui.length(u(0.4)), "display": ui.keyword("none")})
    warm["properties"]["text"] = ui.text("#str_231206")
    doc.show("mbt-warm", warmup)
    infinity = ui.group("mbt-inf", {**ui.absolute(left=u(320 - 7), top=u(BAR_Y + 3.5), width=u(14), height=u(14)),
                                    "display": ui.keyword("none")},
                        hud_svg.icon_nodes("icon_infinity", "mbt-inf-g", 0, 0, u(14), tint=WHITE))
    doc.show("mbt-inf", OR(sudden, review))
    nodes += [clock, urgent, warm, infinity]
    # Without a line under it, the time sits 3 u lower, centred on the plate.
    for ident in ("mbt-clock", "mbt-urgent", "mbt-warm"):
        size = u(9.6 if ident == "mbt-warm" else 15.5)
        face = "lowpixel" if ident == "mbt-warm" else "chain"
        top = u(BAR_Y + 14.6) - size * face_metrics(face)["ascent"]
        doc.bind(ident + ".top", ident, "top", sel(has_sub, top, top + u(3)))
    sub = text_at(doc, "mbt-sub", sub_text, 5.9, [1, 1, 1, 0.55], center=320, baseline=BAR_Y + 22.6, width=TIMER_W + 10,
                  extra={"letter-spacing": ui.length(u(0.55))})
    starts = text_at(doc, "mbt-starts", None, 5.9, [1, 1, 1, 0.55], center=320, baseline=BAR_Y + 22.6, width=TIMER_W + 10,
                     extra={"letter-spacing": ui.length(u(0.55)), "display": ui.keyword("none")})
    starts["properties"]["text"] = ui.text("#str_231207")
    sd = text_at(doc, "mbt-sd", None, 5.9, [1, 1, 1, 0.55], center=320, baseline=BAR_Y + 22.6, width=TIMER_W + 10,
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
        place = {"right": u(640 - (SIDE_X[0] + SIDE_W - 6))} if side == 0 else {"left": u(SIDE_X[1] + 6)}
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
        px = SIDE_X[0] + SIDE_W - 46 if side == 0 else SIDE_X[1] + 6
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


def net_quality(doc: HudDocument) -> dict:
    """Network quality (13.13) at all times, following the bar's trailing end:
    the ping in Chain at 0.75 beside four rising bars, lit by the ping and
    loss."""
    ping = S("oq4_ping")
    lit = net_level(doc)
    bars = []
    for k in range(4):
        h = 2 + k * 1.6
        bar = ui.group(f"mbn-b{k}", {"position": ui.keyword("relative"), "display": ui.keyword("block"), "width": ui.length(u(2.2)),
                                     "height": ui.length(u(h)), "margin-left": ui.length(u(1.2)),
                                     "background-color": ui.colour([1, 1, 1, 0.2])})
        doc.bind(f"mbn-b{k}.bg", bar["id"], "background-color",
                 colour_of([(AND(op("<", k, lit), op(">=", lit, 3)), rgb(OK, 0.9)), (AND(op("<", k, lit), op("==", lit, 2)), rgb("#E3AD36", 0.9)),
                            (op("<", k, lit), rgb(ERR, 0.9))], [1, 1, 1, 0.2]))
        bars.append(bar)
    row = flex_row("mbn", [bound_label(doc, "mbn-ping", op("numberText", ping), 8, [1, 1, 1, 0.75], face="chain", line=10),
                           label("mbn-ms", "#str_231228", 6, [1, 1, 1, 0.6], line=10, **{"margin-left": ui.length(u(1.5)),
                                                                                          "margin-right": ui.length(u(1))}),
                           flex_row("mbn-bars", bars, **{"align-items": ui.keyword("flex-end"), "height": ui.length(u(7))})],
                   **{"position": ui.keyword("absolute"), "left": ui.length(u(SIDE_X[1] + SIDE_W + 6)), "top": ui.length(u(BAR_Y + 8)),
                      "height": ui.length(u(10))})
    return row


def match_bar(doc: HudDocument) -> dict:
    declare(doc, {"marine_score": 0, "strogg_score": 0, "oq4_my_team": 0, "oq4_team_game": False, "oq4_ping": 0, "oq4_zone": 0,
                  "oq4_pips": False, "oq4_state": 0, "oq4_countdown": False, "oq4_timed": False, "oq4_clock_warn": 0, "oq4_clock": "",
                  "showready": False, "overtime": False, "overtimetext": ""})
    for side in (0, 1):
        declare(doc, {f"oq4_mb{side}_name": "", f"oq4_mb{side}_rank": "", f"oq4_mb{side}_score": 0, f"oq4_mb{side}_you": False,
                      f"oq4_mb{side}_lead": False})
    for flag in range(3):
        declare(doc, {f"oq4_flag{flag}_state": 0, f"oq4_flag{flag}_carrier": ""})
    for team in range(2):
        declare(doc, {f"oq4_alive{team}": 0})
    nodes = bar_side(doc, 0) + bar_side(doc, 1) + bar_timer(doc) + under_bar(doc) + [net_quality(doc)]
    group = full("matchbar", nodes, display=ui.keyword("none"))
    doc.show("matchbar", competitive())
    return group


# ------------------------------------------------------------------ team status

def team_list(doc: HudDocument, ident: str, team, *, trailing: bool) -> dict:
    """One team's status (atlas teamList): the team name above, then one
    138x14.5 u plate per player on a 16.5 u pitch with the weapon (an x for
    the dead, whose rows drop to 0.40), the name cut at 46 u, health in Chain
    (#E46D56 at 25 and below, #FFBE23 above 100) and armor at 0.60. Your row
    uses the header-band plate and the marker; a followed player's row is lit
    in the team's text color at 0.22 with the marker."""
    x = 494.0 if trailing else 8.0
    is_marine = op("==", team, 0)
    text_colour = colour_of([(is_marine, rgb(MAR_T))], rgb(STR_T))
    head_kw = {"right": x + LIST_W} if trailing else {"x": x}
    head = text_at(doc, ident + "-head", sel(is_marine, TEAM_NAMES[0], TEAM_NAMES[1]), 6.8, rgb(MAR_T), baseline=LIST_Y - 3,
                   extra={"letter-spacing": ui.length(u(0.7))}, **head_kw)
    doc.bind(ident + "-head.color", ident + "-head", "color", text_colour)
    rows = [head]
    count = sel(is_marine, S("oq4_ts0_count"), S("oq4_ts1_count"))
    for i in range(LIST_ROWS):
        def f(name, i=i):
            return sel(is_marine, S(f"oq4_ts0_{i}_{name}"), S(f"oq4_ts1_{i}_{name}"))
        rid = f"{ident}{i}"
        y = LIST_Y + i * LIST_PITCH
        you, dead, followed = f("you"), f("dead"), f("followed")
        plate = plate_at(rid + "-plate", 0, 0, LIST_W, LIST_ROW, 0.45)
        doc.show(plate["id"], NOT(you))
        band = plate_at(rid + "-band", 0, 0, LIST_W, LIST_ROW, 0.3, BAND, display=ui.keyword("none"))
        doc.show(band["id"], you)
        lit = []
        for t, colour in enumerate((MAR_T, STR_T)):
            node = vec(f"{rid}-lit{t}", [fitted_plate_path("l", u(11.5), r=u(1), fill=ui.solid(rgb(colour, 0.22)), pad=0, top=u(1.5))],
                       {**ui.absolute(left=u(1.5), top=0, width=u(LIST_W - 3), height=u(LIST_ROW)), "display": ui.keyword("none")})
            doc.show(node["id"], AND(followed, op("==", team, t)))
            lit.append(node)
        mark = marker_vec(rid + "-marker", LIST_W + 1 if trailing else -6, 4.5)
        mark["properties"]["display"] = ui.keyword("none")
        doc.show(mark["id"], OR(you, followed))
        icon_x = LIST_W - 21 if trailing else 4
        weapons = []
        for index, glyph in enumerate(MP_ROSTER):
            holder = ui.group(f"{rid}-w{index}", {**ui.absolute(left=u(icon_x), top=u(2.5), width=u(17), height=u(9.5)),
                                                  "display": ui.keyword("none")},
                              hud_svg.icon_nodes(glyph, f"{rid}-w{index}-g", 0, 0, u(9.5), tint=rgb(FEED_COLOURS[index])))
            doc.show(holder["id"], AND(NOT(dead), op("==", f("weapon"), index)))
            weapons.append(holder)
        cross = vec(rid + "-dead", [pts_path("a", [(u(icon_x + 5), u(4)), (u(icon_x + 11), u(10.5))], closed=False,
                                             stroke=stroke([1, 1, 1, 0.4], u(1.4), cap="round")),
                                    pts_path("b", [(u(icon_x + 11), u(4)), (u(icon_x + 5), u(10.5))], closed=False,
                                             stroke=stroke([1, 1, 1, 0.4], u(1.4), cap="round"))], {"display": ui.keyword("none")})
        doc.show(cross["id"], dead)
        name_kw = {"right": LIST_W - 26} if trailing else {"x": 26}
        name = text_at(doc, rid + "-name", f("name"), 9, rgb(MAR_T), baseline=10.6, extra={**ellipsis(46), "opacity": num(1)}, **name_kw)
        doc.bind(rid + "-name.color", rid + "-name", "color", colour_of([(OR(you, followed), WHITE), (is_marine, rgb(MAR_T))], rgb(STR_T)))
        doc.bind(rid + "-name.opacity", rid + "-name", "opacity", sel(dead, 0.4, 1))
        health = text_at(doc, rid + "-hp", op("numberText", f("health")), 10.6, WHITE, baseline=11, face="chain",
                         right=34 if trailing else LIST_W - 34, extra={"display": ui.keyword("none")})
        doc.bind(rid + "-hp.color", rid + "-hp", "color",
                 colour_of([(op("<=", f("health"), 25), rgb(ERR)), (op(">", f("health"), 100), rgb(VALUE))], WHITE))
        doc.show(health["id"], NOT(dead))
        armor = text_at(doc, rid + "-ar", op("numberText", f("armor")), 9, [1, 1, 1, 0.6], baseline=11, face="chain",
                        right=10 if trailing else LIST_W - 10, extra={"display": ui.keyword("none")})
        doc.bind(rid + "-ar.color", rid + "-ar", "color", colour_of([(op(">", f("armor"), 0), [1, 1, 1, 0.6])], [1, 1, 1, 0.25]))
        doc.show(armor["id"], NOT(dead))
        # Where the player is (MP-15): beside the row on the view's side, cut at 78 u.
        place_kw = {"right": -4} if trailing else {"x": LIST_W + 4}
        place = text_at(doc, rid + "-loc", f("location"), 7.4, [1, 1, 1, 0.72], baseline=10.2,
                        extra={**ellipsis(LOCATION_W), "display": ui.keyword("none")}, **place_kw)
        doc.show(place["id"], AND(NOT(dead), op("!=", f("location"), "")))
        row = ui.group(rid, {**ui.absolute(left=u(x), top=u(y), width=u(LIST_W), height=u(LIST_ROW)), "display": ui.keyword("none")},
                       [plate, band, *lit, mark, *weapons, cross, name, health, armor, place])
        doc.show(rid, op(">", count, i))
        rows.append(row)
    return full(ident, rows, display=ui.keyword("none"))


def team_status(doc: HudDocument) -> tuple:
    """Team modes: your team's list at the leading edge; spectators see the
    Marines there and the Strogg at the trailing edge, where held statistics
    take the trailing list's place. Returns (leading, trailing)."""
    declare(doc, {"oq4_spectating": False, "oq4_my_team": 0, "stat_visible": 0})
    for team in range(2):
        declare(doc, {f"oq4_ts{team}_count": 0})
        for i in range(LIST_ROWS):
            declare(doc, {f"oq4_ts{team}_{i}_name": "", f"oq4_ts{team}_{i}_weapon": -1, f"oq4_ts{team}_{i}_health": 0,
                          f"oq4_ts{team}_{i}_armor": 0, f"oq4_ts{team}_{i}_dead": False, f"oq4_ts{team}_{i}_you": False,
                          f"oq4_ts{team}_{i}_followed": False, f"oq4_ts{team}_{i}_location": ""})
    spectating = S("oq4_spectating")
    leading = team_list(doc, "tsl", sel(spectating, 0, S("oq4_my_team")), trailing=False)
    doc.show("tsl", AND(competitive(), team_mode()))
    trailing = team_list(doc, "tst", 1, trailing=True)
    doc.show("tst", AND(competitive(), team_mode(), spectating, op("==", S("stat_visible"), 0)))
    return leading, trailing


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


def item_timers(doc: HudDocument) -> dict:
    """The item timers the server admits this player to (si_itemTimers,
    hud_itemTimers): up to six rows trailing at 633 u from y 96, each a plate
    at 0.35 with the item's name and the time left in Chain (#FFBE23); an item
    that is there reads in #B5C784 with no time. The time of day
    (hud_localClock) sits above them at 0.60."""
    declare(doc, {"oq4_it_count": 0, "oq4_local_clock": ""})
    clock = text_at(doc, "localclock", S("oq4_local_clock"), 7.4, [1, 1, 1, 0.6], baseline=ITEM_Y - 4, face="chain",
                    right=ITEM_RIGHT, extra={"display": ui.keyword("none"), "max-width": ui.length(u(ITEM_W))})
    doc.show("localclock", op("!=", S("oq4_local_clock"), ""))
    rows = [clock]
    x = ITEM_RIGHT - ITEM_W
    for i in range(ITEM_ROWS):
        declare(doc, {f"oq4_it_{i}_name": "", f"oq4_it_{i}_time": "", f"oq4_it_{i}_up": False})
        rid = f"itimer{i}"
        up = S(f"oq4_it_{i}_up")
        plate = plate_at(rid + "-plate", 0, 0, ITEM_W, ITEM_ROW, 0.35)
        # A fixed box: the name is cut only when it would reach the time.
        name = text_at(doc, rid + "-name", S(f"oq4_it_{i}_name"), 6.8, [1, 1, 1, 0.8], x=5, baseline=6.6,
                       extra={**ellipsis(ITEM_W - 34), "width": ui.length(u(ITEM_W - 34))})
        doc.bind(rid + "-name.color", rid + "-name", "color", colour_of([(up, rgb(OK))], [1, 1, 1, 0.8]))
        time = text_at(doc, rid + "-time", S(f"oq4_it_{i}_time"), 7.6, rgb(VALUE), baseline=6.8, face="chain",
                       right=ITEM_W - 5, extra={"max-width": ui.length(u(28))})
        row = ui.group(rid, {**ui.absolute(left=u(x), top=u(ITEM_Y + i * ITEM_PITCH), width=u(ITEM_W), height=u(ITEM_ROW)),
                             "display": ui.keyword("none")}, [plate, name, time])
        doc.show(rid, op(">", S("oq4_it_count"), i))
        rows.append(row)
    return full("itimers", rows)
