#!/usr/bin/env python3
"""Capture the Remastered HUD in windowed gameplay without host input.

Drives the stock HUD GUIs through semantic developer commands (ui_hudTest
state/event, testStatusBar, testWeaponWheel) - the Remastered peer receives
the same state and events - and takes engine screenshots in a hidden window
with mouse and controller input disabled.

    python tools/ui/capture_remastered_hud.py --assets <Quake 4> --output .tmp/<new dir> [--size 1280x720]
"""
import argparse
import json
import re
from pathlib import Path

import capture_retained_screens as capture

ROOT = Path(__file__).resolve().parents[2]


def shot(name):
    return f'screenshot "screenshots/{name}.tga"'


def hud(gui, *ops):
    return [f'ui_hudTest {gui} {op}' for op in ops]


def equip(slot):
    return ['testWeaponWheel open', f'testWeaponWheel slot {slot}', 'testWeaponWheel release']


def on_foot(gui):
    """The single-player on-foot stages, shared by the Marine and Strogg HUDs."""
    return [
        'set g_autoExecAfterMapLoad ""', 'notarget', 'god', 'give all', 'waitMsec 1500',
        'testStatusBar stats 100 57', 'waitMsec 500', shot('base'),
        *equip(5), 'waitMsec 1600', *hud(gui, 'event weaponChange'), 'waitMsec 150', shot('strip'),
        *hud(gui, 'state itemtext "Armor Shard"', 'state itemicon gfx/guis/hud/icons/item_armorshard.tga', 'event itemPickup'),
        'waitMsec 120',
        *hud(gui, 'event itemPickup'), 'waitMsec 120',
        *hud(gui, 'state itemtext "Large Health"', 'state itemicon gfx/guis/hud/icons/item_health.tga', 'event itemPickup'),
        'waitMsec 200', shot('pickups'),
        *hud(gui, 'state rhinochatter 1', 'event radioChatterUp'), 'waitMsec 120', shot('radio-shrinking'),
        'waitMsec 600', shot('radio'),
        'testStatusBar stats 20 0', 'waitMsec 300', shot('low'), 'waitMsec 250', shot('low-pulse'),
        'testStatusBar stats 125 100', 'waitMsec 60', shot('gain'), 'waitMsec 500',
        *hud(gui, 'event playerHit'), 'waitMsec 40', shot('hit'),
        *hud(gui, 'state bracket_left 230', 'state bracket_top 170', 'state bracket_width 180', 'state bracket_height 140',
             'event showBrackets'), 'waitMsec 100', shot('brackets'),
        *hud(gui, 'event hideBrackets', 'event showExit', 'event saveMessage'), 'waitMsec 300', shot('notices'),
        *hud(gui, 'event radioChatterDown', 'event hideExit'),
        'testStatusBar stats 100 100', 'testStatusBar boss 70', 'waitMsec 100', 'testStatusBar stats 55 100', 'waitMsec 300',
        shot('boss'), 'testStatusBar boss 20 warn', 'waitMsec 300', shot('boss-warn'),
        'testStatusBar stats 125 100',
        'set ui_hudScale 1.25', 'set ui_hudSafeInset 4', 'waitMsec 300', shot('scaled'),
        'set ui_hudScale 1', 'set ui_hudSafeInset 0',
        *equip(0), 'waitMsec 1300', shot('unlimited'),
        'set ui_hudLayout 0', 'waitMsec 400', shot('classic'), 'set ui_hudLayout 1', 'waitMsec 400', shot('restored'),
        'set ui_hudLayout 2', *hud(gui, 'event radioChatterUp'), 'waitMsec 400', shot('minimal'),
        *hud(gui, 'event radioChatterDown'), 'set ui_hudLayout 1',
    ]


def in_vehicle(vehicle):
    """A vehicle HUD (guis/vehicles/hud.gui): the player boards through the
    actor's enterVehicle script event, then the stock events drive the rest."""
    gui = 'guis/vehicles/hud.gui'
    return [
        # A map's opening objective notice hides vehicle HUDs for 5 s.
        'set g_autoExecAfterMapLoad ""', 'notarget', 'god', 'waitMsec 9000',
        # Driving off closes a notice the player has not moved away from (64 u).
        f'script "$player1.enterVehicle(${vehicle})"', 'openq4_testInput 1500 forward 127', 'waitMsec 700', shot('veh-enter'),
        'waitMsec 1700', shot('veh-line1'), 'waitMsec 700', shot('veh-line2'),
        *hud(gui, 'event vehicleHit'), 'waitMsec 60', shot('veh-hit'),
        *hud(gui, 'event electricWarningOn'), 'waitMsec 150', shot('veh-elec'), *hud(gui, 'event electricWarningOff'),
        *hud(gui, 'state rhinochatter 1', 'event radioChatterUp'), 'waitMsec 700', shot('veh-radio'),
        *hud(gui, 'event radioChatterDown'), 'testStatusBar boss 60', 'testStatusBar stats 55 0', 'waitMsec 300', shot('veh-boss'),
        'testStatusBar stats 100 0', *hud(gui, 'event hideBossBar', 'event hideBossShieldBar', 'event showLockmessage'), 'waitMsec 200',
        shot('veh-locked'), *hud(gui, 'event hideLockmessage'),
        'set ui_hudScale 1.25', 'waitMsec 300', shot('veh-scaled'), 'set ui_hudScale 1',
        'set ui_hudLayout 0', 'waitMsec 400', shot('veh-classic'), 'set ui_hudLayout 1', 'waitMsec 400', shot('veh-restored'),
        'set ui_hudLayout 2', 'waitMsec 400', shot('veh-minimal'), 'set ui_hudLayout 1',
    ]


def cannon():
    """The air-defence cannon (guis/vehicles/airdef_cannon.gui), which the
    player crews in mcc_landing once the map hands it over: boarding through
    the actor's enterVehicle script event, as the map's shootDoor() does, then
    the stock events drive the transmission display and the prompt's lock."""
    gui = 'guis/vehicles/airdef_cannon.gui'
    return [
        # The map's AI gunner holds the gun until maintainBigGun() ends; clearing its
        # flag lets the loop remove the gunner and unlock the gun, as the level does.
        'set g_autoExecAfterMapLoad ""', 'notarget', 'god', 'waitMsec 3000',
        'script "map_mcc_landing::g_bigGunActive = 0"', 'waitMsec 14000',
        'script "$player1.enterVehicle($vehicle_airdefense_big_gun_1)"', 'waitMsec 1500', shot('cannon'),
        *hud(gui, 'state rhinochatter 1', 'event radioChatterUp'), 'waitMsec 700', shot('cannon-radio'),
        *hud(gui, 'event radioChatterDown', 'event showLockmessage'), 'waitMsec 200', shot('cannon-locked'),
        *hud(gui, 'event hideLockmessage'),
        'set ui_hudLayout 0', 'waitMsec 400', shot('cannon-classic'), 'set ui_hudLayout 1', 'waitMsec 400', shot('cannon-restored'),
        'set ui_hudLayout 2', 'waitMsec 400', shot('cannon-minimal'), 'set ui_hudLayout 1',
    ]


def space_cannon():
    """The Awakening's space cannon (guis/vehicles/airassault/turretplayerhud.gui):
    the player boards m03's top gun through the actor's enterVehicle script
    event, then the script's own ship events drive the panel."""
    gui = 'guis/vehicles/airassault/turretplayerhud.gui'
    return [
        # The level seats an AI gunner in the top gun and hands it over later, as here.
        'set g_autoExecAfterMapLoad ""', 'notarget', 'god', 'waitMsec 9000',
        'script "$topgun_driver.exitVehicle( 1 )"', 'waitMsec 300', 'script "$topgun_driver.remove()"', 'waitMsec 300',
        'script "$topgun.show()"', 'script "$player1.enterVehicle($topgun)"', 'waitMsec 3000', shot('turret'),
        *hud(gui, 'event ship_health_01_on', 'event ship_health_02_on', 'event ship_target_03_on', 'event warning_NE'),
        'waitMsec 300', shot('turret-panel'),
        *hud(gui, 'state rhinochatter 1', 'event radioChatterUp'), 'waitMsec 700', shot('turret-radio'),
        *hud(gui, 'event radioChatterDown'),
        'set ui_hudLayout 0', 'waitMsec 400', shot('turret-classic'), 'set ui_hudLayout 1', 'waitMsec 400', shot('turret-restored'),
        'set ui_hudLayout 2', 'waitMsec 400', shot('turret-minimal'), 'set ui_hudLayout 1',
    ]


def mcc_cannon():
    """The Awakening's MCC cannon (guis/vehicles/mcc/cannonplayerhud.gui): the
    player boards m06's side gun as the map's turret sequence does; the hull
    readout follows mcchealth, which the sequence's own loop also writes."""
    gui = 'guis/vehicles/mcc/cannonplayerhud.gui'
    return [
        'set g_autoExecAfterMapLoad ""', 'notarget', 'god', 'waitMsec 9000',
        'script "$player1.enterVehicle($turret)"', 'waitMsec 3000', shot('mcc'),
        *hud(gui, 'state mcchealth 1100'), 'waitMsec 300', shot('mcc-half'),
        *hud(gui, 'state mcchealth 420', 'state rhinochatter 1', 'event radioChatterUp'), 'waitMsec 700', shot('mcc-warn'),
        *hud(gui, 'event radioChatterDown'),
        'set ui_hudLayout 0', 'waitMsec 400', shot('mcc-classic'), 'set ui_hudLayout 1', 'waitMsec 400', shot('mcc-restored'),
        'set ui_hudLayout 2', 'waitMsec 400', shot('mcc-minimal'), 'set ui_hudLayout 1',
    ]


def chaingun_turret():
    """openQ4's HUD for the Awakening's chaingun turret (guis/vehicles/turret_hud.gui):
    the player boards m01's trench turret through enterVehicle and the gun's own
    fire heats it, past the warning level and then its limit; the script's
    setHealth gives the gunner low health, which god mode then keeps."""
    gui = 'guis/vehicles/turret_hud.gui'
    return [
        'set g_autoExecAfterMapLoad ""', 'notarget', 'god', 'waitMsec 2500',
        'script "$player1.enterVehicle($vehicle_q4x_turret_chaingun_1)"', 'waitMsec 2000', shot('chaingun'),
        'openq4_testInput 7000 attack', 'waitMsec 2500', shot('chaingun-warm'),
        'waitMsec 1800', shot('chaingun-hot'), 'waitMsec 1400', shot('chaingun-overheat'), 'waitMsec 3000',
        'script "$player1.setHealth( 20 );"', *hud(gui, 'state rhinochatter 1', 'event radioChatterUp'), 'waitMsec 700',
        shot('chaingun-low'),
        *hud(gui, 'event radioChatterDown'),
        'openq4_testInput 4500 attack', 'waitMsec 4300',
        'set ui_hudLayout 0', 'waitMsec 300', shot('chaingun-classic'), 'set ui_hudLayout 1', 'waitMsec 400', shot('chaingun-restored'),
        'set ui_hudLayout 2', 'waitMsec 400', shot('chaingun-minimal'), 'set ui_hudLayout 1',
    ]


def speeder():
    """The Awakening's speeder bike (guis/vehicles/speederplayerhud.gui): m07's
    race seats the player on the bike and runs the timer itself; the race's
    checkpoint event and the expiry it counts toward give the progress and the
    last 10 s, and the crouch control fires a boost."""
    gui = 'guis/vehicles/speederplayerhud.gui'
    return [
        'set g_autoExecAfterMapLoad ""', 'notarget', 'god', 'waitMsec 6000', shot('speeder'),
        *hud(gui, 'event showprogress30'), 'waitMsec 200', shot('speeder-progress'),
        'openq4_testInput 300 crouch', 'waitMsec 450', shot('speeder-boost'), 'waitMsec 900', shot('speeder-boost-full'),
        'waitMsec 2400', shot('speeder-recharge'),
        'script "AREA1_TIME_EXPIRED = sys.getTime() + 9.6"', 'waitMsec 500', shot('speeder-last10'),
        *hud(gui, 'state rhinochatter 1', 'event radioChatterUp'), 'waitMsec 700', shot('speeder-radio'),
        *hud(gui, 'event radioChatterDown'),
        'set ui_hudLayout 0', 'waitMsec 400', shot('speeder-classic'), 'set ui_hudLayout 1', 'waitMsec 400', shot('speeder-restored'),
        'set ui_hudLayout 2', 'waitMsec 400', shot('speeder-minimal'), 'set ui_hudLayout 1',
    ]


def gev():
    """The Awakening's GEV (guis/vehicles/gev_hud.gui), which no map places:
    m09's hover tank gives up its spot to the GEV (vehicle_speeder) and the
    player boards through the actor's enterVehicle script event. The damage
    command drains the 10-point shield (it regenerates after 6 s) and, with
    god mode off (the driver's god protects the vehicle from its next frame),
    the hull; the test input selects the machinegun and fires the cannon.
    The GEV's model lacks every joint its def names (eye, turret, gun_pivot,
    muzzle), so it spawns facing yaw 0, where its hull reading is level, and
    the cannon fires last: its kickback flings the GEV and ejects the driver."""
    gui = 'guis/vehicles/gev_hud.gui'
    return [
        'set g_autoExecAfterMapLoad ""', 'notarget', 'god', 'waitMsec 6000',
        'script "$vehicle_gev_1.remove()"', 'setviewpos -5968 -9632 132 0', 'waitMsec 500',
        'spawn vehicle_speeder name gev origin "-6212 -8768 112" angle 0', 'waitMsec 1500',
        'script "$player1.enterVehicle($gev)"', 'waitMsec 2000', shot('gev'),
        *hud(gui, 'state rhinochatter 1', 'event radioChatterUp'), 'waitMsec 700', shot('gev-radio'),
        *hud(gui, 'event radioChatterDown', 'event showLockmessage'), 'waitMsec 200', shot('gev-locked'),
        *hud(gui, 'event hideLockmessage'),
        'set ui_hudLayout 0', 'waitMsec 400', shot('gev-classic'), 'set ui_hudLayout 1', 'waitMsec 400', shot('gev-restored'),
        'set ui_hudLayout 2', 'waitMsec 400', shot('gev-minimal'), 'set ui_hudLayout 1', 'waitMsec 400',
        'damage gev 4', 'waitMsec 300', shot('gev-shields'),
        'god', 'waitMsec 100', 'damage gev 306', 'waitMsec 100', 'god', 'waitMsec 200', shot('gev-hull'),
        'waitMsec 500', shot('gev-pulse'),
        'openq4_testInput 100 impulse 1', 'waitMsec 600', shot('gev-mgun'),
        'openq4_testInput 100 impulse 0', 'waitMsec 600', 'openq4_testInput 200 attack', 'waitMsec 450', shot('gev-charge'),
    ]


def medchange():
    """Medical Labs' implant activation (guis/vehicles/medchange.gui, 14.19):
    the level starts with Kane strapped to the medbed, whose HUD is the
    overlay, so the test command sends the events the map script sends
    (activate as the nanocyte flash fades, hurt with each scream) and the
    save notice; then reduced motion and Classic."""
    gui = 'guis/vehicles/medchange.gui'
    return [
        'set g_autoExecAfterMapLoad ""', 'waitMsec 5000', shot('med-rest'),
        *hud(gui, 'event activate'), 'waitMsec 550', shot('med-0550'), 'waitMsec 1050', shot('med-1600'),
        'waitMsec 1000', shot('med-2600'), 'waitMsec 1650', shot('med-4250'), 'waitMsec 600', shot('med-done'),
        *hud(gui, 'event hurt'), 'waitMsec 300', shot('med-hurt'),
        *hud(gui, 'event saveMessage'), 'waitMsec 500', shot('med-saved'), 'waitMsec 2800',
        'set ui_retainedReducedMotion 1', *hud(gui, 'event activate'), 'waitMsec 1500', shot('med-reduced'),
        'waitMsec 3500', 'set ui_retainedReducedMotion 0',
        'set ui_hudLayout 0', *hud(gui, 'event activate'), 'waitMsec 1500', shot('med-classic'), 'set ui_hudLayout 1',
    ]


def medchange_script():
    """The implant activation through the Medical Labs script itself: the
    level starts with Kane on the medbed; `script` marks it the player's
    turn (the level's own threads clear the flag as its sequence moves on,
    so each call sets it first) and runs nanocyteFade(), whose white flash
    and sendNamedEvent activate the overlay as the stroggification does,
    determineScream() its hurt pulse with the player's scream, and a real
    save the game's saveMessage. A second activation mid-sequence restarts
    the clocks; then reduced motion."""
    turn = 'script map_medlabs::playersTurn = 1'
    fade = [turn, 'script map_medlabs::nanocyteFade()']
    return [
        'set g_autoExecAfterMapLoad ""', 'waitMsec 5000', shot('implant-rest'),
        *fade, 'waitMsec 550', shot('implant-0550'),
        'waitMsec 1050', shot('implant-1600'), 'waitMsec 2650', shot('implant-4250'), 'waitMsec 600', shot('implant-hidden'),
        turn, 'script map_medlabs::determineScream()', 'waitMsec 300', shot('implant-hurt'),
        'saveGame "implant"', 'waitMsec 600', shot('implant-saved'), 'waitMsec 2500',
        *fade, 'waitMsec 2000', *fade, 'waitMsec 550', shot('implant-restart-0550'),
        'waitMsec 4300', shot('implant-restart-hidden'),
        'set ui_retainedReducedMotion 1', *fade, 'waitMsec 1500', shot('implant-reduced'), 'waitMsec 3500',
        'set ui_retainedReducedMotion 0',
    ]

def objectives(gui):
    """The objectives display through the held scores button (the game
    publishes the list), then the notices and a failure through the stock
    GUI's own events."""
    return [
        'set g_autoExecAfterMapLoad ""', 'notarget', 'god', 'waitMsec 7000', shot('obj-start'),
        'waitMsec 5000', 'openq4_testInput 2000 scores', 'waitMsec 700', shot('obj-display'), 'waitMsec 1600',
        *hud(gui, 'state oq4_new_title "Destroy the three Strogg communication relays before the convoy leaves"',
             'state oq4_new_shot gfx/objectives/none', 'event newObjective'), 'waitMsec 400', shot('obj-new'),
        *hud(gui, 'state oq4_done_title "Reach the hangar control room"', 'event newObjectiveComplete'), 'waitMsec 400',
        shot('obj-both'),
        *hud(gui, 'event oq4_closeNewObjective'), 'waitMsec 300', shot('obj-complete'),
        *hud(gui, 'event oq4_closeDoneObjective', 'state oq4_failed_title "Protect the convoy"', 'event objectiveFailed'),
        'waitMsec 300', shot('obj-failed'),
        'set ui_hudLayout 0', *hud(gui, 'event newObjective'), 'waitMsec 400', shot('obj-classic'),
    ]


def objectives_toggle():
    """Toggle Objectives (g_objectivesToggle 1): a press of the scores button
    opens the display, which stays open with its Close prompt until the next
    press."""
    return [
        'set g_autoExecAfterMapLoad ""', 'notarget', 'god', 'set g_objectivesToggle 1', 'waitMsec 12000',
        'openq4_testInput 150 scores', 'waitMsec 1500', shot('obj-toggle-open'),
        'openq4_testInput 150 scores', 'waitMsec 700', shot('obj-toggle-closed'),
        'set ui_hudLayout 0', 'openq4_testInput 150 scores', 'waitMsec 1000', shot('obj-toggle-classic'),
        'openq4_testInput 150 scores', 'waitMsec 300', 'set ui_hudLayout 1', 'set g_objectivesToggle 0',
    ]


def quick_chat_stages(team):
    """The quick-chat wheel (guis/hud/quick_chat.q4ui): opened and a phrase
    picked through testQuickChat; in Team DM it opens on TEAM and switches to
    ALL. Releasing sends the phrase, which the chat lines then show; reduced
    motion and Minimal keep the wheel."""
    tag = 'quickchat-team' if team else 'quickchat'
    steps = ['set g_autoExecAfterMapLoad ""', 'waitMsec 2500', 'addbot', 'addbot', 'waitMsec 3000',
             'testQuickChat open', 'waitMsec 400', shot(f'{tag}-open'), 'testQuickChat slot 2', 'waitMsec 300', shot(f'{tag}-slot')]
    if team:
        steps += ['testQuickChat channel', 'waitMsec 300', shot(f'{tag}-all'), 'testQuickChat channel']
    steps += ['testQuickChat slot 7', 'waitMsec 200', 'testQuickChat release', 'waitMsec 700', shot(f'{tag}-sent'),
              'set ui_retainedReducedMotion 1', 'testQuickChat open', 'testQuickChat slot 5', 'waitMsec 150', shot(f'{tag}-reduced'),
              'testQuickChat cancel', 'set ui_retainedReducedMotion 0',
              'set ui_hudLayout 2', 'testQuickChat open', 'testQuickChat slot 0', 'waitMsec 400', shot(f'{tag}-minimal'),
              'testQuickChat cancel', 'set ui_hudLayout 1']
    return steps


def glyph_stages():
    """Controller prompts (13.5): testPromptDevice names the controller and
    in_promptStyle forces each glyph family. The respawn prompt names the
    attack control (RT) and the quick-chat wheel's hub the zoom control (LT);
    the keyboard's keycaps return last."""
    steps = ['set g_autoExecAfterMapLoad ""', 'waitMsec 2500', 'addbot', 'addbot', 'waitMsec 2500', 'kill 0', 'waitMsec 3600']
    for style, name in ((1, 'xbox'), (2, 'playstation'), (3, 'nintendo'), (4, 'deck'), (5, 'generic')):
        steps += [f'set in_promptStyle {style}', 'testPromptDevice controller', 'waitMsec 300', shot(f'glyph-respawn-{name}')]
    steps += ['set in_promptStyle 1', 'testPromptDevice controller', 'testQuickChat open', 'testQuickChat slot 0', 'waitMsec 400',
              shot('glyph-quickchat-xbox'), 'set in_promptStyle 2', 'waitMsec 300', shot('glyph-quickchat-playstation'),
              'testQuickChat cancel', 'set in_promptStyle 0', 'testPromptDevice desktop', 'waitMsec 300', shot('glyph-respawn-keyboard')]
    return steps


def summary_stages(name, play):
    """Bots play until the match ends, then the summary: a chat line, the
    chat field typing (openq4_summaryChat), then its SCOREBOARD and
    STATISTICS tabs in turn, the retained card's through its tab events and
    the stock summary's as a click would (each command does nothing to the
    other)."""
    return [
        'set g_autoExecAfterMapLoad ""', 'waitMsec 2500', *play, 'say Good game everyone, see you on the next map', 'waitMsec 600',
        shot(f'summary-{name}'),
        'openq4_summaryChat Rematch on Phrantic?', 'waitMsec 400', shot(f'summary-{name}-typing'), 'openq4_summaryChat stop',
        'openq4_guiAction b_score', 'openq4_retainedGui event tab_scoreboard', 'waitMsec 500', shot(f'summary-{name}-scoreboard'),
        'openq4_guiAction b_stats', 'openq4_retainedGui event tab_statistics', 'waitMsec 500', shot(f'summary-{name}-stats'),
    ]


def arena(gui, r, b, players=None, winner=None, focus=None):
    """One arena of the bracket chart through the stock scoreboard's own
    events (rvTourneyGUI::ArenaStart, ArenaDone, ArenaSelect)."""
    ops = [f'state round {r}', f'state bracket {b}', f'state empty {0 if players else 1}']
    for slot, (name, score) in enumerate(players or [], start=1):
        ops += [f'state name{slot}_round{r}_bracket{b} "{name}"', f'state score{slot}_round{r}_bracket{b} {score}']
    ops.append('event arenaInit')
    if winner:
        ops += [f'state winner {winner}', 'event arenaDone']
    if focus is not None:
        ops += [f'state sel {focus}', 'event arenaFocus']
    return hud(gui, *ops)


def bracket_stages():
    """A twelve-player tournament in its quarter-finals, sent as the game sends
    it: the preliminaries decided (two arenas empty), two quarter-finals
    decided, yours (player one) and a followed one still running."""
    gui = 'guis/scoreboard.gui'
    names = ['Anderson', 'Strauss', 'Bidwell', 'Voss', 'Morris', 'Cortez', 'Rhodes', 'Tetzlaff', 'Player', 'Gunner', 'Sorg',
             'Hollenbeck']
    first = [arena(gui, 1, b + 1, [(names[2 * b], 1 if b % 2 == 0 else 0), (names[2 * b + 1], 0 if b % 2 == 0 else 1)],
                   winner=1 if b % 2 == 0 else 2) for b in range(6)]
    first += [arena(gui, 1, 7), arena(gui, 1, 8)]
    second = [arena(gui, 2, 1, [('Anderson', 1), ('Voss', 0)], winner=1),
              arena(gui, 2, 2, [('Morris', 0), ('Tetzlaff', 1)], winner=2),
              arena(gui, 2, 3, [('Player', 0), ('Hollenbeck', 0)], focus=1),
              arena(gui, 2, 4)]
    return [op for ops in first + second for op in ops]


def scroll_stages(name, bots):
    """The board held open with more rows than fit, at its top, three rows
    down and at its end."""
    return [
        'set g_autoExecAfterMapLoad ""', 'waitMsec 2500', *['addbot'] * bots, 'waitMsec 8000', 'testScoreboard 30000',
        'waitMsec 800', 'ui_scoreboardScrolls', shot(f'scroll-{name}-top'),
        'scoreboardScroll 3', 'waitMsec 400', shot(f'scroll-{name}-3'),
        'scoreboardScroll 40', 'waitMsec 400', shot(f'scroll-{name}-end'), 'ui_scoreboardScrolls',
    ]


SCENARIOS = {
    'marine': ('game/airdefense1', on_foot('guis/hud.gui')),
    'objectives': ('game/airdefense1', objectives('guis/wristcomm.gui')),
    'objectives-toggle': ('game/airdefense1', objectives_toggle()),
    'tank': ('game/convoy1', in_vehicle('tank_1')),
    'airdef': ('game/mcc_landing', cannon()),
    'turret': ('game/m03_airassault', space_cannon()),
    'mcc': ('game/m06_mcc_invasion', mcc_cannon()),
    'chaingun': ('game/m01_stranarus_trench1', chaingun_turret()),
    'speeder': ('game/m07_race1', speeder()),
    'gev': ('game/m09_valkaryne', gev()),
    'medchange': ('game/medlabs', medchange()),
    'medchange-script': ('game/medlabs', medchange_script()),
    'strogg': ('game/recomp', on_foot('guis/hud_strogg.gui')),
    'mp': ('mp:mp/q4dm1', [
        'set g_autoExecAfterMapLoad ""', 'waitMsec 2500', shot('mp-base'),
        'addbot', 'addbot', 'waitMsec 2500', shot('mp-bots'),
        'testScoreboard 1200', 'waitMsec 400', shot('mp-scoreboard'), 'waitMsec 1000',
        'say Remastered chat line from the capture', 'waitMsec 300', shot('mp-chat'),
        'kill 1', 'waitMsec 400', shot('mp-feed'), 'kill 0', 'waitMsec 400', shot('mp-feed-you'),
        # The fall holds a respawn for 3 s; then the prompt names the attack control.
        'waitMsec 3200', shot('mp-respawn'), 'waitMsec 2800', shot('mp-later'),
        *equip(2), 'waitMsec 250', shot('mp-strip'),
        'set ui_hudScale 1.25', 'waitMsec 300', shot('mp-scaled'), 'set ui_hudScale 1',
        'set ui_hudLayout 0', 'waitMsec 400', shot('mp-classic'), 'set ui_hudLayout 1', 'waitMsec 400', shot('mp-restored'),
        'set ui_hudLayout 2', 'waitMsec 400', shot('mp-minimal'), 'set ui_hudLayout 1',
        # The chat input over the history (chatInput fills the entry without device input).
        'clientMessageMode', 'waitMsec 300', 'chatInput "Rail on the bridge, quad in ten"', 'waitMsec 400', shot('mp-chat-input'),
        'chatCaret 4', 'waitMsec 300', shot('mp-chat-caret'),
        'chatHistory up', 'waitMsec 300', shot('mp-chat-history'),
    ]),
    # A spectator follows a bot (the follow card with Next, Previous and Free
    # camera), then flies free from its view: name tags over the players in
    # view. Warm-up keeps the match from ending as abandoned when the only
    # human leaves play.
    'mp-spectator': ('mp:mp/q4dm1', [
        'set g_autoExecAfterMapLoad ""', 'waitMsec 2500', 'addbot', 'addbot', 'addbot', 'waitMsec 4000',
        'set ui_spectate Spectate', 'waitMsec 1500', 'follownext', 'waitMsec 1500', shot('spec-follow'),
        'followfree', 'waitMsec 400', shot('spec-free-1'), 'waitMsec 1200', shot('spec-free-2'),
        'follownext', 'waitMsec 2500', 'followfree', 'waitMsec 400', shot('spec-free-3'), 'waitMsec 1300', shot('spec-free-4'),
        'follownext', 'waitMsec 2500', 'followfree', 'waitMsec 400', shot('spec-free-5'),
        'set ui_hudLayout 0', 'waitMsec 400', shot('spec-classic'), 'set ui_hudLayout 1', 'set ui_spectate Play',
    ]),
    'mp-pace': ('mp:mp/q4dm1', [
        'set g_autoExecAfterMapLoad ""', 'waitMsec 2500', 'set ui_hudProfile 2', 'waitMsec 8000', 'set ui_hudProfile 0',
        'echo PACE remastered', 'com_speeds 1', 'waitMsec 1500', 'com_speeds 0',
        'set ui_hudLayout 0', 'waitMsec 1000', 'echo PACE classic', 'com_speeds 1', 'waitMsec 1500', 'com_speeds 0',
    ]),
    'sp-pace': ('game/airdefense1', [
        'set g_autoExecAfterMapLoad ""', 'waitMsec 2500', 'set ui_hudProfile 30', 'waitMsec 4000', 'set ui_hudProfile 0',
        'echo PACE remastered', 'com_speeds 1', 'waitMsec 1500', 'com_speeds 0',
        'set ui_hudLayout 0', 'waitMsec 1000', 'echo PACE classic', 'com_speeds 1', 'waitMsec 1500', 'com_speeds 0',
    ]),
    'mp-tourney': ('mp:mp/q4dm1', [
        'set g_autoExecAfterMapLoad ""', 'waitMsec 2500', 'addbot', 'addbot', 'addbot', 'waitMsec 4000', shot('tourney-hud'),
        'testScoreboard 1500', 'waitMsec 500', shot('tourney-scoreboard'), 'waitMsec 1200',
    ]),
    # The bracket chart as the tournament runs: eight players in four arenas,
    # a frag limit of one, the board held early and again once arenas are
    # decided. The bots ready themselves; `ready` readies the human.
    'mp-tourney-chart': ('mp:mp/q4dm1', [
        'set g_autoExecAfterMapLoad ""', 'waitMsec 2500', *['addbot'] * 7, 'waitMsec 3000', 'ready', 'waitMsec 9000',
        'testScoreboard 1500', 'waitMsec 600', shot('chart-early'), 'waitMsec 60000',
        'testScoreboard 1500', 'waitMsec 600', shot('chart-later'), 'waitMsec 1200',
    ]),
    # The chart's states without a tournament to play: decided arenas, empty
    # ones, yours and the links between them (bracket_stages).
    'mp-tourney-bracket': ('mp:mp/q4dm1', [
        'set g_autoExecAfterMapLoad ""', 'waitMsec 2500', *['addbot'] * 5, 'waitMsec 5000', 'testScoreboard 6000', 'waitMsec 400',
        *bracket_stages(), 'waitMsec 900', shot('bracket-quarters'),
    ]),
    'mp-ctf': ('mp:mp/q4ctf1', [
        'set g_autoExecAfterMapLoad ""', 'waitMsec 2500', shot('ctf-base'),
        'addbot', 'addbot', 'addbot', 'waitMsec 3000', shot('ctf-bots'),
        'testScoreboard 1200', 'waitMsec 400', shot('ctf-scoreboard'), 'waitMsec 1000',
        'sayTeam team line from the capture', 'waitMsec 300', shot('ctf-chat'),
        'set ui_hudLayout 0', 'waitMsec 400', shot('ctf-classic'), 'set ui_hudLayout 1', 'waitMsec 400',
    ]),
    # Competitive (ui_hudLayout 3): the match bar with network quality, the
    # team status and the enlarged status bar; Remastered again for contrast.
    'mp-competitive': ('mp:mp/q4dm1', [
        'set g_autoExecAfterMapLoad ""', 'waitMsec 2500', 'addbot', 'addbot', 'addbot', 'waitMsec 4000', shot('comp-dm'),
        'say Competitive chat line from the capture', 'waitMsec 300', shot('comp-dm-chat'),
        'kill 1', 'waitMsec 400', shot('comp-dm-feed'), 'waitMsec 6000', shot('comp-dm-later'),
        'set ui_hudLayout 1', 'waitMsec 400', shot('comp-dm-remastered'), 'set ui_hudLayout 3',
    ]),
    'mp-competitive-ctf': ('mp:mp/q4ctf1', [
        'set g_autoExecAfterMapLoad ""', 'waitMsec 2500', 'addbot', 'addbot', 'addbot', 'waitMsec 4000', shot('comp-ctf'),
        'waitMsec 8000', shot('comp-ctf-later'), 'kill 0', 'waitMsec 400', shot('comp-ctf-dead'),
    ]),
    # Spectating in warm-up (the only human spectating a live match abandons
    # it): both teams' lists, the followed row lit, then the free camera's tags
    # with their health lines.
    'mp-competitive-spec': ('mp:mp/q4ctf1', [
        'set g_autoExecAfterMapLoad ""', 'waitMsec 2500', 'addbot', 'addbot', 'addbot', 'waitMsec 4000',
        'set ui_spectate Spectate', 'waitMsec 1500', 'follownext', 'waitMsec 1500', shot('comp-spec-follow'),
        'followfree', 'waitMsec 400', shot('comp-spec-free-1'), 'waitMsec 1200', shot('comp-spec-free-2'),
        'follownext', 'waitMsec 2500', 'followfree', 'waitMsec 400', shot('comp-spec-free-3'), 'set ui_spectate Play',
    ]),
    # FLOW-058's evidence at each --size: eight players a team, a long name
    # and the chat input open. (A vote called by the only human passes at
    # once; the native checks place the vote card.)
    'mp-competitive-matrix': ('mp:mp/q4ctf1', [
        'set g_autoExecAfterMapLoad ""', 'waitMsec 2500', *['addbot'] * 15, 'waitMsec 6000', shot('matrix-ctf'),
        'clientMessageMode', 'waitMsec 300', 'chatInput Meet at the railgun on the upper walkway', 'waitMsec 600', shot('matrix-chat'),
        'chatInput Their quad respawns in thirty seconds so hold the lower ramp until it is up and then push through the '
        'teleporter together', 'waitMsec 400', shot('matrix-chat-long'),
    ]),
    # The match summary (14.14) once three bots reach a frag limit of one
    # (deathmatch, Team DM), or a Clan Arena duel's single round ends with your
    # elimination (its round modes share Team DM's layout), held open by
    # g_gameReviewPause, with its SCOREBOARD and STATISTICS tabs opened as a
    # click would (openq4_guiAction).
    'mp-summary': ('mp:mp/q4dm1', summary_stages('dm', ['addbot', 'addbot', 'addbot', 'waitMsec 120000'])),
    'mp-summary-team': ('mp:mp/q4dm1', summary_stages('team', ['addbot', 'addbot', 'addbot', 'waitMsec 150000'])),
    'mp-summary-ca': ('mp:mp/q4dm1', summary_stages('ca', ['addbot', 'waitMsec 20000', 'kill 0', 'waitMsec 15000'])),
    # The quick-chat wheel (14.6) without input: testQuickChat opens it, picks a
    # phrase, switches the channel and releases, which sends the phrase; in
    # Team DM (mp-quickchat-team) and deathmatch (mp-quickchat).
    'mp-quickchat': ('mp:mp/q4dm1', quick_chat_stages(False)),
    'mp-quickchat-team': ('mp:mp/q4dm1', quick_chat_stages(True)),
    # Controller prompts (13.5) in Team DM: each glyph family on the respawn prompt
    # and the quick-chat wheel's channel switch.
    'mp-glyphs': ('mp:mp/q4dm1', glyph_stages()),
    # The held scoreboard past its height (14.14): 22 players in deathmatch and
    # 20 in Team DM at the smallest pitch, scrolled by scoreboardScroll, the
    # command the weapon-cycle controls and the right stick send while it is
    # held; ui_scoreboardScrolls prints the engine's switch to the log.
    'mp-scoreboard-scroll': ('mp:mp/q4dm1', scroll_stages('dm', 21)),
    'mp-scoreboard-scroll-team': ('mp:mp/q4dm1', scroll_stages('team', 19)),
}


AWAKENING = {'turret', 'mcc', 'chaingun', 'speeder', 'gev'}
COMPETITIVE = {'mp-competitive', 'mp-competitive-ctf', 'mp-competitive-spec', 'mp-competitive-matrix'}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, default=ROOT / '.install')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), default='gl')
    parser.add_argument('--size', default='1280x720')
    parser.add_argument('--scenario', action='append', choices=sorted(SCENARIOS))
    parser.add_argument('--timeout', type=int, default=240)
    parser.add_argument('--awakening', type=Path, help="content root holding The Awakening's q4xbase (expansion scenarios)")
    parser.add_argument('--gate', choices=('0', '1'), default='1', help='ui_retained (0 draws the stock GUIs, for comparison)')
    parser.add_argument('--language', default='english', help='sys_lang for every run (the notices come from its tables)')
    args = parser.parse_args()
    args.output = args.output.resolve()
    if args.output.exists() or not args.output.is_relative_to((ROOT / '.tmp').resolve()):
        parser.error('use a fresh output directory below .tmp')
    results = []
    for scenario in args.scenario or ['marine']:
        capture.SCENARIOS[scenario] = SCENARIOS[scenario]
        capture.SCENARIO_CVARS[scenario] = {'ui_hudLayout': '1', 'ui_hudScale': '1', 'ui_hudSafeInset': '0', 'ui_hudWidth': '1'}
        # A live match at once (no warm-up), timed, so the feed, notices and clock run.
        capture.SCENARIO_CVARS[scenario].update({'si_warmup': '0', 'si_timeLimit': '10', 'si_fragLimit': '10'})
        if scenario in ('mp-ctf', 'mp-competitive-ctf', 'mp-competitive-spec', 'mp-competitive-matrix'):
            capture.SCENARIO_CVARS[scenario]['si_gameType'] = 'CTF'
        if scenario.startswith('mp-summary'):
            capture.SCENARIO_CVARS[scenario].update({'si_fragLimit': '1', 'si_roundLimit': '1', 'g_gameReviewPause': '600'})
        if scenario in ('mp-quickchat-team', 'mp-glyphs'):
            capture.SCENARIO_CVARS[scenario]['si_gameType'] = 'Team DM'
        if scenario == 'mp-summary-team':
            capture.SCENARIO_CVARS[scenario]['si_gameType'] = 'Team DM'
        if scenario == 'mp-summary-ca':
            capture.SCENARIO_CVARS[scenario]['si_gameType'] = 'Clan Arena'
        if scenario.startswith('mp-scoreboard-scroll'):
            capture.SCENARIO_CVARS[scenario].update({'si_maxPlayers': '24', 'si_fragLimit': '0', 'si_timeLimit': '0'})
        if scenario == 'mp-scoreboard-scroll-team':
            capture.SCENARIO_CVARS[scenario]['si_gameType'] = 'Team DM'
        if scenario == 'mp-competitive-matrix':
            capture.SCENARIO_CVARS[scenario].update({'si_maxPlayers': '16', 'ui_name': 'Lieutenant Matthew Kane of Rhino Squad'})
        if scenario in COMPETITIVE:
            capture.SCENARIO_CVARS[scenario]['ui_hudLayout'] = '3'
        if scenario == 'mp-tourney':
            capture.SCENARIO_CVARS[scenario]['si_gameType'] = 'Tourney'
        if scenario == 'mp-tourney-chart':
            capture.SCENARIO_CVARS[scenario].update({'si_gameType': 'Tourney', 'si_fragLimit': '1', 'si_maxPlayers': '8'})
        if scenario == 'mp-tourney-bracket':
            capture.SCENARIO_CVARS[scenario].update({'si_gameType': 'Tourney', 'si_maxPlayers': '16'})
        if scenario in ('mp-spectator', 'mp-competitive-spec'):
            capture.SCENARIO_CVARS[scenario]['si_warmup'] = '1'
        # The Awakening's scenarios run its game directory over baseoq4.
        args.game_dir = 'q4xbase' if scenario in AWAKENING else 'baseoq4'
        if scenario in AWAKENING:
            if not args.awakening:
                parser.error(f'--awakening is required for {scenario}')
            capture.SCENARIO_CVARS[scenario].update({'fs_awakeningpath': str(args.awakening.resolve()), 'fs_game_base': 'baseoq4',
                                                     'com_gameMode': 'SP', 'si_gameType': 'singleplayer',
                                                     'com_nextGameModule': 'game_sp'})
        result = capture.run(args, scenario)
        log = (ROOT / result['log']).read_text(encoding='utf-8', errors='replace') if result['log'] else ''
        result['remastered_warnings'] = [line for line in log.splitlines() if 'Remastered HUD' in line or 'retained:' in line.lower()]
        result['loaded'] = re.findall(r'RETAINED_GUI_LOADED (\S*hud\S*)', log)
        results.append(result)
        (args.output / 'report.json').write_text(json.dumps(results, indent=2) + '\n')
        print(scenario, 'complete' if result['complete'] else 'INCOMPLETE', 'rc', result['returncode'],
              len(result['screenshots']), 'shots', 'loaded', result['loaded'], 'warnings', result['remastered_warnings'][:5],
              'errors', result['errors'][:5], flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
