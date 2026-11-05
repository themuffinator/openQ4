#!/usr/bin/env python3
"""Capture the ui_retained title, pause and loading screens in the engine.

Each scenario is one hidden, windowed client with a private savepath under
.tmp, installed Quake 4 assets and host mouse/controller input disabled. Stages
are console scripts: engine `screenshot` captures, `ui_retainedStatus` reports
and semantic `openq4_retainedGui` operations; no OS input or OS screenshot is
used. With --gate 0 the title and pause scenarios (whose real map load covers
the loading route) qualify that nothing retained loads; the loading scenario
opens its document with the developer `testGUI` command, which is not gated.
The default, --gate default, leaves ui_retained unset, as a player has it.

The fallback scenarios run as a private mod (fs_game) in the savepath, whose
files take precedence over openQ4's packs as any mod's do: a retained document
that cannot load, or a main menu without a page the home screens hand off to,
as a mod's own menu might be. Each screen must then present its stock GUI, and
ui_retainedStatus lists the fallback. A mod's own copy of a stock loading
screen presents instead of the retained one, which never loads.

    python tools/ui/capture_retained_screens.py --assets "E:/SteamLibrary/steamapps/common/Quake 4" \
        --runtime .install --output .tmp/retained-screens/gl-720 --renderer gl --size 1280x720
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import socket
import struct
import subprocess
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DONE = 'RETAINED_SCREENS_CAPTURE_COMPLETE'

# Scenario -> (map to load or None for the title, console stages).
# Splits a scenario: what follows runs after a map its page launches loads.
AFTER_MAP = '@aftermap'
# Where a scenario logs the game module its page reloads.
MODULE_LOG = 'openq4-match.log'

# The prompt presentations, in order: the keyboard's keycaps, then each
# controller family in_promptStyle forces.
PROMPT_STYLES = ((0, 'keyboard'), (1, 'xbox'), (2, 'playstation'), (3, 'nintendo'), (4, 'steamdeck'), (5, 'generic'))


def prompt_style(style: int) -> list[str]:
    """Steps that show one presentation: testPromptDevice stands in for the
    device used last, so no device is touched."""
    return [f'set in_promptStyle {style}', f'testPromptDevice {"controller" if style else "desktop"}']


def menu_step(direction: str) -> list[str]:
    """Steps that press one semantic menu direction (no device input) and
    report where the focus went."""
    return [f'openq4_retainedGui menu {direction} 1', f'openq4_retainedGui menu {direction} 0', 'waitMsec 400',
            'openq4_retainedGui report']


SCENARIOS = {
    'title': (None, [
        'waitMsec 4000', 'ui_retainedStatus', 'screenshot "screenshots/title.tga"',
        # Depth: with the pointer at the top-trailing corner the backdrop layers lean away.
        'openq4_retainedGui state pointer_x 1', 'openq4_retainedGui state pointer_y -1', 'waitMsec 300',
        'screenshot "screenshots/title-depth.tga"', 'openq4_retainedGui state pointer_x 0', 'openq4_retainedGui state pointer_y 0',
        'openq4_retainedGui focus nav_loadgame', 'waitMsec 500', 'screenshot "screenshots/title-focus.tga"',
        'openq4_retainedGui event exitModalShow', 'waitMsec 500', 'ui_retainedStatus', 'screenshot "screenshots/title-exit.tga"',
        'openq4_retainedGui event exitModalHide', 'waitMsec 400',
        'openq4_retainedGui focus nav_settings', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 250', 'screenshot "screenshots/title-depart.tga"', 'waitMsec 1500', 'ui_retainedStatus',
        'screenshot "screenshots/title-settings.tga"',
    ]),
    # Prompts follow the active device (section 13.5): the title's prompt bar
    # shows the keyboard's keycaps and then each controller family, Confirm
    # kept south.
    'prompts': (None, [
        'waitMsec 4000', 'set in_swapConfirmBack 1',
        *[step for style, name in PROMPT_STYLES for step in (
            *prompt_style(style), 'waitMsec 400',
            f'openq4_retainedGui inspect "prompt-231042-{"cap" if name == "keyboard" else name}"',
            f'openq4_retainedGui inspect "prompt-200013-{"cap" if name == "keyboard" else name}"',
            f'screenshot "screenshots/prompts-{name}.tga"')],
        # Swapped, SELECT shows the east button and EXIT the south one: the
        # automatic swap of a forced Nintendo style, then Xbox with the swap
        # forced on.
        *[step for setting, style, name in ((0, 3, 'nintendo'), (2, 1, 'xbox')) for step in (
            f'set in_swapConfirmBack {setting}', *prompt_style(style), 'waitMsec 400',
            f'openq4_retainedGui inspect "prompt-231042-{name}-swapped"',
            f'openq4_retainedGui inspect "prompt-200013-{name}-swapped"',
            f'screenshot "screenshots/prompts-{name}-swapped.tga"')],
        'set in_swapConfirmBack 0', *prompt_style(0),
    ]),
    # The multiplayer Escape card on a listen server (opted in): each
    # presentation on the strip's tab keys and the card's prompt bar, the
    # shoulders for Q and E, Confirm kept south.
    'mpcard-prompts': ('mp:mp/q4dm1', [
        'openq4_assertMenuActivation 4000 game', 'waitMsec 2500', 'ui_retainedStatus', 'set in_swapConfirmBack 1',
        *[step for style, name in PROMPT_STYLES for step in (
            *prompt_style(style), 'waitMsec 500',
            *[f'openq4_retainedGui inspect "{key}-{"cap" if name == "keyboard" else name}"'
              for key in ('tab-next', 'prompt-back-0', 'prompt-tabs-0', 'prompt-select-0')],
            f'screenshot "screenshots/mpcard-prompts-{name}.tga"')],
        'set in_swapConfirmBack 0', *prompt_style(0),
    ]),
    # The triggers' glyphs at the trailing end of the Match and Admin pages'
    # section strips: none for the keyboard, then each controller family's,
    # and the triggers stepping the Match page's sections.
    'mpcard-triggers': ('mp:mp/q4dm1', [
        'openq4_assertMenuActivation 4000 game', 'waitMsec 2500', 'ui_retainedStatus',
        'openq4_retainedGui event tab_match', 'waitMsec 800',
        *[step for style, name in PROMPT_STYLES for step in (
            *prompt_style(style), 'waitMsec 500',
            'openq4_retainedGui inspect "match-triggers"',
            *([] if name == 'keyboard' else [f'openq4_retainedGui inspect "match-triggers-lt-{name}"',
                                             f'openq4_retainedGui inspect "match-triggers-rt-{name}"']),
            f'screenshot "screenshots/mpcard-triggers-{name}.tga"')],
        *prompt_style(1), 'openq4_retainedGui event onSectionNext', 'waitMsec 500',
        'screenshot "screenshots/mpcard-triggers-next.tga"',
        'openq4_retainedGui event tab_admin', 'waitMsec 800', 'openq4_retainedGui inspect "admin-triggers-rt-xbox"',
        'screenshot "screenshots/mpcard-triggers-admin.tga"', *prompt_style(0),
    ]),
    # The Escape card's Admin page on a listen server, whose host is the
    # admin: the players (two bots), a bot chosen, Kick's confirmation and the
    # bot kicked, View ban list,
    # the Server section with a frag limit drafted and applied, and the
    # console running status.
    'mpadmin': ('mp:mp/q4dm1', [
        'openq4_assertMenuActivation 4000 game', 'waitMsec 2500', 'ui_retainedStatus',
        'openq4_retainedGui event tab_admin', 'waitMsec 600', 'openq4_retainedGui report', 'openq4_retainedGui inspect "admin-live"',
        'openq4_retainedGui inspect "admin-player-0"', 'openq4_retainedGui inspect "admin-player-1"', 'openq4_retainedGui inspect "admin-player-2"',
        'screenshot "screenshots/mpadmin-players.tga"',
        'openq4_retainedGui focus "admin-player-1"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 400',
        'openq4_retainedGui inspect "admin-player-1-chosen"', 'openq4_retainedGui inspect "admin-kick"',
        'screenshot "screenshots/mpadmin-chosen.tga"',
        # Kick asks first in the stock confirmation, naming the bot; YES kicks it.
        'openq4_retainedGui focus "admin-kick"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 600',
        'openq4_retainedGui inspect "adminKickModal"', 'openq4_retainedGui inspect "adminKickModal-name"',
        'screenshot "screenshots/mpadmin-kick-confirm.tga"',
        'openq4_retainedGui focus "adminKickModal_yes"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 1500',
        'openq4_retainedGui inspect "admin-player-2"', 'screenshot "screenshots/mpadmin-kicked.tga"',
        'openq4_retainedGui focus "admin-bans"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 600',
        'openq4_retainedGui inspect "banModal"', 'screenshot "screenshots/mpadmin-bans.tga"',
        'openq4_retainedGui event banModalHide', 'waitMsec 600',
        'openq4_retainedGui event admin_section_server', 'waitMsec 400', 'openq4_retainedGui focus "admin-row-fraglimit"',
        *[step for _ in range(5) for step in ('openq4_retainedGui menu right 1', 'openq4_retainedGui menu right 0', 'waitMsec 150')],
        'waitMsec 400', 'openq4_retainedGui inspect "admin-row-fraglimit-value"', 'openq4_retainedGui inspect "admin-apply"',
        'screenshot "screenshots/mpadmin-server.tga"',
        'openq4_retainedGui focus "admin-apply"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 2000',
        'si_fragLimit', 'ui_retainedStatus',
        'openq4_assertMenuActivation 4000 game', 'waitMsec 1500', 'openq4_retainedGui event tab_admin', 'waitMsec 400',
        'openq4_retainedGui event admin_section_console', 'waitMsec 400', 'openq4_retainedGui focus "admin-command"',
        'openq4_retainedGui number begin "admin-command"', 'openq4_retainedGui number input "admin-command" "serverInfo"',
        'openq4_retainedGui number commit "admin-command"', 'waitMsec 1000', 'openq4_retainedGui inspect "admin-console-0"',
        'openq4_retainedGui inspect "admin-console-11"', 'screenshot "screenshots/mpadmin-console.tga"',
    ]),
    # The Menu button on the Admin page (section 13.5): with a frag limit
    # drafted on the Server section it presses Apply changes, which applies it
    # and closes the card; reopened on the Players section it closes the card.
    'mpadmin-menu': ('mp:mp/q4dm1', [
        'openq4_assertMenuActivation 4000 game', 'waitMsec 2500', 'si_fragLimit', 'ui_retainedStatus',
        'openq4_retainedGui event tab_admin', 'waitMsec 600',
        'openq4_retainedGui event admin_section_server', 'waitMsec 400', 'openq4_retainedGui focus "admin-row-fraglimit"',
        *[step for _ in range(3) for step in ('openq4_retainedGui menu right 1', 'openq4_retainedGui menu right 0', 'waitMsec 150')],
        'waitMsec 400', 'openq4_retainedGui inspect "admin-row-fraglimit-value"', 'openq4_retainedGui inspect "admin-apply"',
        'screenshot "screenshots/mpadmin-menu-pending.tga"',
        'openq4_retainedGui event onMenuButton', 'waitMsec 2000', 'si_fragLimit', 'ui_retainedStatus',
        'screenshot "screenshots/mpadmin-menu-applied.tga"',
        'openq4_assertMenuActivation 4000 game', 'waitMsec 1500', 'openq4_retainedGui event tab_admin', 'waitMsec 400',
        'openq4_retainedGui event admin_section_players', 'waitMsec 400', 'openq4_retainedGui inspect "admin-apply"',
        'openq4_retainedGui event onMenuButton', 'waitMsec 1200', 'ui_retainedStatus', 'screenshot "screenshots/mpadmin-menu-closed.tga"',
    ]),
    # The Menu button's glyph at the end of the Admin page's Apply changes
    # plate, with a frag limit drafted: none for the keyboard, then each
    # controller family's.
    'mpadmin-menu-glyph': ('mp:mp/q4dm1', [
        'openq4_assertMenuActivation 4000 game', 'waitMsec 2500', 'openq4_retainedGui event tab_admin', 'waitMsec 600',
        'openq4_retainedGui event admin_section_server', 'waitMsec 400', 'openq4_retainedGui focus "admin-row-fraglimit"',
        'openq4_retainedGui menu right 1', 'openq4_retainedGui menu right 0', 'waitMsec 600',
        *[step for style, name in PROMPT_STYLES for step in (
            *prompt_style(style), 'waitMsec 500', 'openq4_retainedGui inspect "admin-apply-menu"',
            *([] if name == 'keyboard' else [f'openq4_retainedGui inspect "admin-apply-menu-{name}"']),
            f'screenshot "screenshots/mpadmin-menu-glyph-{name}.tga"')],
        *prompt_style(0),
    ]),
    # The Admin page from a client of a dedicated server (SCENARIO_SERVERS):
    # the password first; a wrong one is refused, the server's own makes the
    # player an admin, and the console sends a command. (A client's console
    # appends its rcon to the command buffer, as the stock page did, so the
    # command and its reply follow the rest of this script.)
    'mpadminremote': (None, [
        'waitMsec 6000', 'set logFileName "logs/openq4-match.log"',
        f'connect 127.0.0.1:{27964}', AFTER_MAP,
        'openq4_assertMenuActivation 4000 game', 'waitMsec 2500', 'ui_retainedStatus',
        'openq4_retainedGui event tab_admin', 'waitMsec 600', 'openq4_retainedGui report', 'openq4_retainedGui inspect "admin-pass"',
        'screenshot "screenshots/mpadminremote-password.tga"',
        'openq4_retainedGui number begin "admin-password"', 'openq4_retainedGui number input "admin-password" "not the password"',
        'openq4_retainedGui number commit "admin-password"', 'waitMsec 3000', 'openq4_retainedGui inspect "admin-refused"',
        'screenshot "screenshots/mpadminremote-refused.tga"',
        'openq4_retainedGui number begin "admin-password"', 'openq4_retainedGui number input "admin-password" "openq4-admin-probe"',
        'openq4_retainedGui number commit "admin-password"', 'waitMsec 3000', 'openq4_retainedGui inspect "admin-live"',
        'openq4_retainedGui inspect "admin-player-0"', 'screenshot "screenshots/mpadminremote-admin.tga"',
        'openq4_retainedGui event admin_section_console', 'waitMsec 400', 'openq4_retainedGui focus "admin-command"',
        'openq4_retainedGui number begin "admin-command"', 'openq4_retainedGui number input "admin-command" "serverInfo"',
        'openq4_retainedGui number commit "admin-command"', 'waitMsec 1000', 'openq4_retainedGui inspect "admin-console-0"',
        'openq4_retainedGui inspect "admin-console-11"', 'screenshot "screenshots/mpadminremote-console.tga"',
        'disconnect', 'waitMsec 2000',
    ]),
    # Campaign's two fourth-level children: QUAKE 4 is chosen (the rail) and
    # NEW GAME descends to its difficulty page carrying QUAKE 4, BACK climbs
    # back onto NEW GAME; CHAPTERS descends to the levels carrying its own
    # label, and their BACK climbs back onto CHAPTERS.
    'campaignskill': (None, [
        'waitMsec 4000', 'campaignMenu', 'waitMsec 1200',
        'openq4_retainedGui focus campaign', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1200', 'openq4_retainedGui focus quake4', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 400', 'openq4_retainedGui report', 'openq4_retainedGui inspect "prompts"',
        'screenshot "screenshots/campaignskill-campaign.tga"',
        'openq4_retainedGui focus newgame', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1200', 'ui_retainedStatus', 'openq4_retainedGui report', 'openq4_retainedGui inspect "prompts"',
        'screenshot "screenshots/campaignskill.tga"',
        'openq4_retainedGui focus back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1200', 'ui_retainedStatus', 'openq4_retainedGui report', 'screenshot "screenshots/campaignskill-back.tga"',
        'openq4_retainedGui focus chapters', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 150', 'screenshot "screenshots/campaignskill-chapters-descending.tga"',
        'waitMsec 1050', 'ui_retainedStatus', 'openq4_retainedGui report', 'screenshot "screenshots/campaignskill-chapters.tga"',
        'openq4_retainedGui focus chapter_back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1200', 'ui_retainedStatus', 'openq4_retainedGui report', 'screenshot "screenshots/campaignskill-chapters-back.tga"',
    ]),
    'pause': ('game/airdefense1', [
        'openq4_assertMenuActivation 10000', 'waitMsec 1200', 'ui_retainedStatus', 'screenshot "screenshots/pause.tga"',
        'openq4_retainedGui focus nav_restart', 'waitMsec 400', 'screenshot "screenshots/pause-focus.tga"',
        'openq4_retainedGui event quitModalShow', 'waitMsec 500', 'screenshot "screenshots/pause-quit.tga"',
        # The modal holds focus until its 300 ms leave completes.
        'openq4_retainedGui event quitModalHide', 'waitMsec 600',
        # RESUME closes the screen at once; the softened view releases over 250 ms.
        'openq4_retainedGui focus nav_resume', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 100', 'ui_retainedStatus', 'screenshot "screenshots/pause-releasing.tga"',
        'waitMsec 700', 'ui_retainedStatus', 'screenshot "screenshots/pause-resumed.tga"',
    ]),
    # The level block's open objectives: airdefense1's first two objective
    # entities fire before the pause menu opens (trigger works in single player).
    'pause-objectives': ('game/airdefense1', [
        'trigger objectiveIntro', 'waitMsec 500', 'trigger objectiveMedic', 'waitMsec 1500',
        'openq4_assertMenuActivation 10000', 'waitMsec 1200', 'ui_retainedStatus', 'screenshot "screenshots/pause-objectives.tga"',
        'openq4_retainedGui focus nav_resume', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 500',
    ]),
    # From game/medlabs on Kane is Strogg and the pause takes the Strogg family:
    # its labels arrive in runes and translate, and focus runs the scan bar.
    'pause-strogg': ('game/recomp', [
        'openq4_assertMenuActivation 10000', 'waitMsec 300', 'screenshot "screenshots/pause-strogg-translating.tga"',
        'waitMsec 1500', 'ui_retainedStatus', 'screenshot "screenshots/pause-strogg.tga"',
        'openq4_retainedGui focus nav_restart', 'waitMsec 120', 'screenshot "screenshots/pause-strogg-focus.tga"',
        'openq4_retainedGui event objectivesShow', 'waitMsec 1200', 'screenshot "screenshots/pause-strogg-objectives.tga"',
        'openq4_retainedGui event objectivesHide', 'waitMsec 1300',
        'openq4_retainedGui event quitModalShow', 'waitMsec 500', 'screenshot "screenshots/pause-strogg-quit.tga"',
        'openq4_retainedGui event quitModalHide', 'waitMsec 600',
        'openq4_retainedGui focus nav_resume', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 100', 'ui_retainedStatus', 'screenshot "screenshots/pause-strogg-releasing.tga"',
        'waitMsec 700', 'ui_retainedStatus', 'screenshot "screenshots/pause-strogg-resumed.tga"',
    ]),
    # The prompt bar names what Accept does (section 13.4): on LOAD GAME's
    # page LOAD on the chosen save, SELECT on another and DELETE on DELETE
    # GAME; on SAVE GAME's page SELECT on a save, then SAVE once it is chosen.
    'prompt-verbs': ('game/airdefense1', [
        'waitMsec 1000', 'saveGame "Alpha"', 'waitMsec 1500', 'saveGame "Bravo"', 'waitMsec 1500',
        'openq4_assertMenuActivation 10000', 'waitMsec 1800', 'ui_retainedStatus',
        'openq4_retainedGui focus nav_loadgame', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1100', 'ui_retainedStatus', 'openq4_retainedGui report',
        'openq4_retainedGui focus "row-0"', 'waitMsec 300', 'openq4_retainedGui report', 'screenshot "screenshots/prompt-load-chosen.tga"',
        'openq4_retainedGui focus "row-1"', 'waitMsec 300', 'openq4_retainedGui report', 'screenshot "screenshots/prompt-load-other.tga"',
        'openq4_retainedGui focus load_delete', 'waitMsec 300', 'openq4_retainedGui report', 'screenshot "screenshots/prompt-load-delete.tga"',
        'openq4_retainedGui focus load_back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 1300',
        'openq4_retainedGui focus nav_savegame', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1100', 'ui_retainedStatus',
        'openq4_retainedGui focus "row-1"', 'waitMsec 300', 'openq4_retainedGui report', 'screenshot "screenshots/prompt-save-other.tga"',
        'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 400',
        'openq4_retainedGui report', 'screenshot "screenshots/prompt-save-chosen.tga"',
        'openq4_retainedGui focus save_back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 1300',
        'openq4_retainedGui focus nav_resume', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 800', 'ui_retainedStatus', 'screenshot "screenshots/prompt-resumed.tga"',
    ]),
    # A save names the run's difficulty and play time: a new game on
    # airdefense1 completes its first objective, saves and loads that save
    # (the completed objective survives), then leaves for airdefense2, whose
    # pause adds the run's total to the mission time; a save there names both
    # in CONTINUE's detail and on the Load Game page. The scenario runs on
    # through the load and the level change, so it clears its own re-run.
    'save-details': ('game/airdefense1', [
        'set g_autoExecAfterMapLoad ""',
        'trigger objectiveIntro', 'waitMsec 500', 'trigger objectiveMedic', 'waitMsec 500', 'trigger completeObjectiveIntro',
        'waitMsec 4000', 'saveGame "Bunker"', 'waitMsec 1500', 'loadGame "Bunker"', 'waitMsec 4000',
        'openq4_assertMenuActivation 10000', 'waitMsec 1500', 'ui_retainedStatus', 'screenshot "screenshots/details-pause-loaded.tga"',
        'openq4_retainedGui focus nav_resume', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 800',
        'trigger target_endlevel_2', 'waitMsec 25000',
        'openq4_assertMenuActivation 10000', 'waitMsec 1500', 'ui_retainedStatus', 'screenshot "screenshots/details-pause-next.tga"',
        'openq4_retainedGui focus nav_resume', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 800',
        'saveGame "Trenches"', 'waitMsec 1500', 'disconnect', 'waitMsec 4000', 'ui_retainedStatus', 'openq4_retainedGui report',
        'screenshot "screenshots/details-title.tga"',
        'openq4_retainedGui focus nav_loadgame', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1300', 'ui_retainedStatus', 'openq4_retainedGui report', 'screenshot "screenshots/details-load.tga"',
        'openq4_retainedGui focus "row-2"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 400',
        'openq4_retainedGui report', 'screenshot "screenshots/details-load-older.tga"',
    ]),
    # The triggers page lists (section 13.5): twenty saves, then Load Game from
    # the title, focus paging a visible page down twice, to the list's end and
    # back to its start, as PageDown, End and Home (the triggers' inputs) do.
    'list-paging': ('game/airdefense1', ['waitMsec 1000'] + [command for index in range(20) for command in (
        f'saveGame page{index:02d}', 'waitMsec 150')] + [
        'disconnect', 'waitMsec 6000', 'openq4_retainedGui focus nav_loadgame', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 2500', 'openq4_retainedGui focus "row-0"', 'waitMsec 400',
        'screenshot "screenshots/paging-start.tga"',
        'openq4_retainedGui menu pageDown 1', 'openq4_retainedGui menu pageDown 0', 'waitMsec 500', 'openq4_retainedGui report',
        'screenshot "screenshots/paging-down.tga"',
        'openq4_retainedGui menu pageDown 1', 'openq4_retainedGui menu pageDown 0', 'waitMsec 500', 'openq4_retainedGui report',
        'screenshot "screenshots/paging-down2.tga"',
        'openq4_retainedGui menu end 1', 'openq4_retainedGui menu end 0', 'waitMsec 500', 'openq4_retainedGui report',
        'screenshot "screenshots/paging-end.tga"',
        'openq4_retainedGui menu home 1', 'openq4_retainedGui menu home 0', 'waitMsec 500', 'openq4_retainedGui report',
        'screenshot "screenshots/paging-home.tga"',
    ]),
    # The Menu button closes the menus and resumes play from the pause's pages
    # (section 13.5): the Load and then the Save Game page, each left by their
    # onMenuButton, which the controller's Menu button runs.
    'menu-resume': ('game/airdefense1', [
        'waitMsec 1000', 'saveGame "Menu"', 'waitMsec 1500', 'openq4_assertMenuActivation 10000', 'waitMsec 1800',
        'openq4_retainedGui focus nav_loadgame', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1300', 'ui_retainedStatus', 'screenshot "screenshots/menu-load.tga"',
        'openq4_retainedGui event onMenuButton', 'waitMsec 900', 'ui_retainedStatus', 'screenshot "screenshots/menu-load-resumed.tga"',
        'openq4_assertMenuActivation 10000', 'waitMsec 1800',
        'openq4_retainedGui focus nav_savegame', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1300', 'ui_retainedStatus', 'screenshot "screenshots/menu-save.tga"',
        'openq4_retainedGui event onMenuButton', 'waitMsec 900', 'ui_retainedStatus', 'screenshot "screenshots/menu-save-resumed.tga"',
    ]),
    # Settings > Game Options > Controller's Prompt Style and Swap Confirm and
    # Back rows in the classic in-game menu (SCENARIO_CVARS turns the retained
    # screens off): scrolled to them, their values driven from the console,
    # then the page's last step, which the two rows lengthened.
    'settings-controller': ('game/airdefense1', [
        'openq4_assertMenuActivation 10000', 'waitMsec 1500', 'GuiEvent fromMp_toGameoptions', 'waitMsec 2000',
        'openq4_guiSet gui::gui_set_game_scroll 33', 'waitMsec 1000', 'screenshot "screenshots/settings-controller.tga"',
        'set in_promptStyle 2', 'set in_swapConfirmBack 2', 'waitMsec 800', 'in_promptStyle', 'in_swapConfirmBack',
        'screenshot "screenshots/settings-controller-set.tga"', 'set in_promptStyle 0', 'set in_swapConfirmBack 0',
        'openq4_guiSet gui::gui_set_game_scroll 75', 'waitMsec 1000', 'screenshot "screenshots/settings-controller-end.tga"',
    ]),
    # The Strogg pause's confirmations ask in its family: each label arrives
    # in runes as the dialog's contents show and translates. QUIT TO MENU is
    # taken mid-translation and settled, with YES focused, then EXIT, and SAVE
    # GAME with an existing save's name asks to overwrite it; NO keeps it.
    'confirm-strogg': ('game/recomp', [
        'waitMsec 1000', 'saveGame "Strogg door"', 'waitMsec 1500',
        'openq4_assertMenuActivation 10000', 'waitMsec 1800', 'ui_retainedStatus',
        'openq4_retainedGui event quitModalShow', 'waitMsec 260', 'screenshot "screenshots/confirm-strogg-quit-runes.tga"',
        'waitMsec 700', 'openq4_retainedGui report', 'screenshot "screenshots/confirm-strogg-quit.tga"',
        'openq4_retainedGui focus quitModal_yes', 'waitMsec 300', 'screenshot "screenshots/confirm-strogg-quit-yes.tga"',
        'openq4_retainedGui event quitModalHide', 'waitMsec 700',
        'openq4_retainedGui event exitModalShow', 'waitMsec 1000', 'screenshot "screenshots/confirm-strogg-exit.tga"',
        'openq4_retainedGui event exitModalHide', 'waitMsec 700',
        'openq4_retainedGui focus nav_savegame', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1100', 'ui_retainedStatus', 'openq4_retainedGui focus "save-name"', 'openq4_retainedGui number begin "save-name"',
        'openq4_retainedGui number input "save-name" "Strogg door"', 'openq4_retainedGui number commit "save-name"',
        'waitMsec 1000', 'openq4_retainedGui report', 'screenshot "screenshots/confirm-strogg-overwrite.tga"',
        'openq4_retainedGui focus overwriteModal_no', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 700', 'openq4_retainedGui focus save_back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1300', 'openq4_retainedGui focus nav_resume', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 800', 'ui_retainedStatus', 'screenshot "screenshots/confirm-strogg-resumed.tga"',
    ]),
    # The Strogg pause's pages: SAVE GAME, LOAD GAME and RESTART LEVEL hand
    # off to the Strogg family's pages as the Marine pause's do to theirs. A
    # typed name saves at once; a row's first Accept selects its save; a
    # plate chooses a difficulty; Back returns the Strogg pause each time.
    'pages-strogg': ('game/recomp', [
        'waitMsec 1000', 'saveGame "Recomp entry"', 'waitMsec 1500', 'saveGame "Strogg door"', 'waitMsec 1500',
        'openq4_assertMenuActivation 10000', 'waitMsec 1800', 'ui_retainedStatus',
        'openq4_retainedGui focus nav_savegame', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 250', 'screenshot "screenshots/savegame-strogg-handoff.tga"', 'waitMsec 800', 'ui_retainedStatus',
        'openq4_retainedGui report', 'screenshot "screenshots/savegame-strogg.tga"',
        'openq4_retainedGui focus "save-name"', 'openq4_retainedGui number begin "save-name"',
        'openq4_retainedGui number input "save-name" "Nexus core"', 'waitMsec 300', 'screenshot "screenshots/savegame-strogg-typing.tga"',
        'openq4_retainedGui number commit "save-name"', 'waitMsec 1500', 'openq4_retainedGui report',
        'screenshot "screenshots/savegame-strogg-saved.tga"',
        'openq4_retainedGui focus save_back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1300', 'ui_retainedStatus', 'screenshot "screenshots/pages-strogg-home.tga"',
        'openq4_retainedGui focus nav_loadgame', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1100', 'ui_retainedStatus', 'openq4_retainedGui report', 'screenshot "screenshots/loadgame-strogg.tga"',
        'openq4_retainedGui focus "row-1"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 300',
        'screenshot "screenshots/loadgame-strogg-selected.tga"',
        'openq4_retainedGui focus load_delete', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 500',
        'screenshot "screenshots/loadgame-strogg-delete.tga"',
        'openq4_retainedGui focus deleteModal_no', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 500',
        'openq4_retainedGui focus load_back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 1300',
        'openq4_retainedGui focus nav_restart', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1100', 'ui_retainedStatus', 'openq4_retainedGui report', 'screenshot "screenshots/restartlevel-strogg.tga"',
        'openq4_retainedGui focus "skill-3"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 300',
        'screenshot "screenshots/restartlevel-strogg-chosen.tga"',
        'openq4_retainedGui focus restart_go', 'waitMsec 300', 'screenshot "screenshots/restartlevel-strogg-go.tga"',
        'openq4_retainedGui focus restart_back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1300', 'ui_retainedStatus', 'screenshot "screenshots/pages-strogg-returned.tga"',
    ]),
    # The OBJECTIVES page: airdefense1's first objective completed and two
    # more held, so the page lists two plates and one completed row.
    'pause-objectives-page': ('game/airdefense1', [
        'trigger objectiveIntro', 'waitMsec 500', 'trigger objectiveMedic', 'waitMsec 500', 'trigger completeObjectiveIntro',
        'waitMsec 500', 'trigger objectiveReturnMedic', 'waitMsec 1500',
        'openq4_assertMenuActivation 10000', 'waitMsec 1200', 'screenshot "screenshots/objectives-menu.tga"',
        'openq4_retainedGui event objectivesShow', 'waitMsec 300', 'screenshot "screenshots/objectives-docking.tga"',
        'waitMsec 900', 'ui_retainedStatus', 'screenshot "screenshots/objectives-page.tga"',
        'openq4_retainedGui menu down 1', 'openq4_retainedGui menu down 0', 'waitMsec 300', 'screenshot "screenshots/objectives-scrolled.tga"',
        'openq4_retainedGui event objectivesHide', 'waitMsec 900', 'screenshot "screenshots/objectives-closed.tga"',
        'openq4_retainedGui focus nav_resume', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 600', 'ui_retainedStatus',
    ]),
    # The right stick on the OBJECTIVES page (section 13.5): held down for
    # 250 ms it scrolls the list, which holds no control, 180 dp while the
    # focus stays put; held up for a second it returns to the top. The map
    # holds two open objectives, which fit, so four test rows join them after
    # the pause publishes its state.
    'pause-objectives-stick': ('game/airdefense1', [
        'trigger objectiveIntro', 'waitMsec 500', 'trigger objectiveMedic', 'waitMsec 500', 'trigger completeObjectiveIntro',
        'waitMsec 500', 'trigger objectiveReturnMedic', 'waitMsec 1500',
        'openq4_assertMenuActivation 10000', 'waitMsec 1200',
        *[f'openq4_retainedGui pending pause_objective_{row} "Test objective {row}"' for row in range(2, 6)],
        *[f'openq4_retainedGui pending pause_objective_text_{row} "A test row that makes the list overflow."' for row in range(2, 6)],
        'openq4_retainedGui state pause_objective_count 6', 'waitMsec 200',
        'openq4_retainedGui event objectivesShow', 'waitMsec 1200', 'openq4_retainedGui report',
        'screenshot "screenshots/objectives-stick-top.tga"',
        'openq4_retainedGui stick 0 1 250', 'waitMsec 300', 'openq4_retainedGui report',
        'screenshot "screenshots/objectives-stick-down.tga"',
        'openq4_retainedGui stick 0 -1 1000', 'waitMsec 300', 'screenshot "screenshots/objectives-stick-up.tga"',
        'openq4_retainedGui stick 0 -1 100',
        'openq4_retainedGui event objectivesHide', 'waitMsec 900',
        'openq4_retainedGui focus nav_resume', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 600', 'ui_retainedStatus',
    ]),
    'loading': (None, [
        'waitMsec 3000', 'testGUI "guis/loading/loading.q4ui"', 'waitMsec 300',
        'openq4_retainedGui pending loading_levelshot gfx/guis/loadscreens/airdefense',
        'openq4_retainedGui pending loading_levelname "Air Defense Trenches"',
        'openq4_retainedGui pending loading_objectives "Fight through the trench network to the anti-aircraft battery."',
        'openq4_retainedGui pending loading_detail Corporal', 'openq4_retainedGui pending loading_phase ASSETS',
        'openq4_retainedGui pending loading_count "412/1630"',
        'openq4_retainedGui state map_loading 0.62', 'waitMsec 600', 'screenshot "screenshots/loading.tga"',
        # Pending values commit with the next state operation.
        'openq4_retainedGui pending loading_phase "LOAD TIME"', 'openq4_retainedGui pending loading_count "12.4 s"',
        'openq4_retainedGui event FinishedLoading', 'openq4_retainedGui state map_loading 1', 'waitMsec 600',
        'screenshot "screenshots/loading-ready.tga"',
        # A controller's prompt: an Xbox pad stands in for one used last.
        *prompt_style(1), 'waitMsec 300', 'openq4_retainedGui inspect "progress-prompt-glyph-xbox"',
        'screenshot "screenshots/loading-controller.tga"', *prompt_style(0),
        # Single player tips while the level loads, as the session publishes
        # them: a tip in slot A, then the next cross-fading in slot B (two of
        # the longest). A key must be quoted: the tokenizer splits an unquoted '#'.
        'openq4_retainedGui pending loading_phase ASSETS', 'openq4_retainedGui pending loading_count "980/1630"',
        'openq4_retainedGui pending loading_ready 0', 'openq4_retainedGui state map_loading 0.8',
        'openq4_retainedGui state loading_tip_a "#str_230066"', 'openq4_retainedGui event tipA', 'waitMsec 400',
        'screenshot "screenshots/loading-tip.tga"',
        'openq4_retainedGui state loading_tip_b "#str_230068"', 'openq4_retainedGui event tipB', 'waitMsec 100',
        'screenshot "screenshots/loading-tip-crossfade.tga"', 'waitMsec 400', 'screenshot "screenshots/loading-tip-next.tga"',
        'openq4_retainedGui pending server_name "openQ4 Test Server"', 'openq4_retainedGui pending server_ip "192.168.1.20:28004"',
        'openq4_retainedGui pending server_limit "Frag limit 30 - Time limit 10"', 'openq4_retainedGui pending server_gametype Deathmatch',
        'openq4_retainedGui pending loading_detail Deathmatch',
        'openq4_retainedGui pending loading_levelshot gfx/guis/loadscreens/q4dm1',
        'openq4_retainedGui pending loading_levelname "Bloodwork"', 'openq4_retainedGui pending loading_ready 0',
        'openq4_retainedGui state loading_mp 1', 'waitMsec 600', 'screenshot "screenshots/loading-mp.tga"',
        # The arsenal as the session publishes it: each kind's image and tint,
        # then its fade (hud.mtr's codes for the railgun, rockets and mega health).
        'openq4_retainedGui state load_icon_src_1 gfx/guis/hud/icons/item_railgun', 'openq4_retainedGui state load_icon_r_1 0',
        'openq4_retainedGui state load_icon_b_1 0', 'openq4_retainedGui state load_icon_1 1', 'openq4_retainedGui event arsenal1',
        'openq4_retainedGui state load_icon_src_2 gfx/guis/hud/icons/item_rocket', 'openq4_retainedGui state load_icon_g_2 0.2',
        'openq4_retainedGui state load_icon_b_2 0', 'openq4_retainedGui state load_icon_2 1', 'openq4_retainedGui event arsenal2',
        'openq4_retainedGui state load_icon_src_3 gfx/guis/hud/icons/item_health', 'openq4_retainedGui state load_icon_r_3 0',
        'openq4_retainedGui state load_icon_g_3 0.5', 'openq4_retainedGui state load_icon_3 1', 'openq4_retainedGui event arsenal3',
        'waitMsec 400', 'screenshot "screenshots/loading-mp-arsenal.tga"',
        # The card's players by team over a spectators and connecting line, and
        # the server's message in the notices color, wrapped to three lines, as
        # the session publishes them; the card grows upward from its foot.
        # `lines` joins its arguments with line breaks.
        'openq4_retainedGui pending server_team_mode 1', 'openq4_retainedGui pending server_roster_known 1',
        'openq4_retainedGui pending server_roster_rows 4', 'openq4_retainedGui lines server_team_a Kane Rhodes Strauss Cortez',
        'openq4_retainedGui lines server_team_b Makron Gladiator "Iron Maiden"',
        'openq4_retainedGui pending server_roster_extra "Spectating: 1 \u00b7 Connecting: 2"',
        'openq4_retainedGui pending server_gametype "Team Deathmatch"', 'openq4_retainedGui pending loading_detail "Team Deathmatch"',
        'openq4_retainedGui state server_message "Welcome to the openQ4 test server. Teams are balanced every map. Be excellent to each other."',
        'waitMsec 300', 'screenshot "screenshots/loading-mp-roster.tga"',
        # Outside team modes one list holds everyone, six names and "+N more".
        'openq4_retainedGui pending server_team_mode 0', 'openq4_retainedGui pending server_roster_rows 7',
        'openq4_retainedGui lines server_players Kane Rhodes Strauss Cortez Sledge Bidwell "+3 more"',
        'openq4_retainedGui pending server_roster_extra "Spectating: 2"', 'openq4_retainedGui pending server_gametype Deathmatch',
        'openq4_retainedGui pending loading_detail Deathmatch', 'openq4_retainedGui state server_message "Frag limit 30. No camping."',
        'waitMsec 300', 'screenshot "screenshots/loading-mp-players.tga"',
        'testGUI', 'waitMsec 300',
    ]),
    # A title document that cannot load: the stock title presents.
    'fallback-document': (None, [
        'waitMsec 4000', 'ui_retainedStatus', 'screenshot "screenshots/fallback-document.tga"',
    ]),
    # A main menu without the Demos page, as a mod's own menu might be: it
    # presents its own home screen, and the pause screen does not cover it either.
    'fallback-menu': (None, [
        'waitMsec 4000', 'ui_retainedStatus', 'screenshot "screenshots/fallback-menu.tga"',
    ]),
    # Section 13.4's wrapping menus at the title, without a save to continue:
    # Up from SINGLE PLAYER runs on to the plinth's MODS, Left wraps to EXIT
    # and Down returns to the top past the hidden CONTINUE.
    'menu-wrap-title': (None, [
        'waitMsec 4000', 'openq4_retainedGui focus nav_singleplayer', 'waitMsec 400', 'openq4_retainedGui report',
        'screenshot "screenshots/menu-wrap-title.tga"',
        *menu_step('up'), 'screenshot "screenshots/menu-wrap-title-up.tga"',
        *menu_step('left'), 'screenshot "screenshots/menu-wrap-title-left.tga"',
        *menu_step('down'), 'screenshot "screenshots/menu-wrap-title-down.tga"',
    ]),
    # The pause: Up from RESUME wraps to EXIT and Down from EXIT to RESUME.
    'menu-wrap-pause': ('game/airdefense1', [
        'openq4_assertMenuActivation 10000', 'waitMsec 1500', 'openq4_retainedGui focus nav_resume', 'waitMsec 400',
        *menu_step('up'), 'screenshot "screenshots/menu-wrap-pause-up.tga"',
        *menu_step('down'), 'screenshot "screenshots/menu-wrap-pause-down.tga"',
    ]),
    # The pause's QUIT TO MENU dialog wraps Left from YES to NO, and Restart
    # Level's difficulties, arriving on the level's own (Corporal), wrap Up
    # past the first to RESTART LEVEL.
    'menu-wrap-dialogs': ('game/airdefense1', [
        'openq4_assertMenuActivation 10000', 'waitMsec 1500', 'openq4_retainedGui event quitModalShow', 'waitMsec 900',
        'openq4_retainedGui focus quitModal_yes', 'waitMsec 300', 'openq4_retainedGui report',
        *menu_step('left'), 'screenshot "screenshots/menu-wrap-dialog-left.tga"',
        'openq4_retainedGui event quitModalHide', 'waitMsec 900',
        'openq4_retainedGui focus nav_restart', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1300', 'openq4_retainedGui report',
        *menu_step('up'), *menu_step('up'), 'screenshot "screenshots/menu-wrap-restart-up.tga"',
    ]),
    # Section 13.4's list focus memory on the pause's Load Game page with
    # three saves and the start autosave: the newest save has focus as the
    # page arrives; from LOAD GAME, Right returns to it rather than to the
    # row nearest the plate.
    'list-memory': ('game/airdefense1', [
        'waitMsec 1000', 'saveGame "First"', 'waitMsec 1200', 'saveGame "Second"', 'waitMsec 1200', 'saveGame "Third"',
        'waitMsec 1200', 'openq4_assertMenuActivation 10000', 'waitMsec 1800',
        'openq4_retainedGui focus nav_loadgame', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1500', 'openq4_retainedGui report', 'screenshot "screenshots/list-memory-arrived.tga"',
        'openq4_retainedGui focus load_go', 'waitMsec 400', 'openq4_retainedGui report',
        'screenshot "screenshots/list-memory-plate.tga"',
        *menu_step('right'), 'screenshot "screenshots/list-memory-returned.tga"',
    ]),
    # The retained SYSTEM page lacks some stock settings, so by default the
    # SYSTEM button opens the stock page.
    'system-default': (None, [
        'waitMsec 4000', 'openq4_guiAction main_b_settings', 'waitMsec 1500', 'openq4_guiAction set_b_system',
        'waitMsec 1500', 'openq4_guiGet p_settings_sys::visible', 'openq4_system report', 'ui_retainedStatus',
        'screenshot "screenshots/system-default.tga"',
    ]),
    # Opted into, the retained SYSTEM page opens from the SYSTEM button, its
    # focus on the performance preset (the report's focus=) on the GRAPHICS
    # tab; then SCREEN, IMAGE QUALITY with a list unfolded, and Back.
    'system-optin': (None, [
        'waitMsec 9000', 'openq4_guiAction main_b_settings', 'waitMsec 1500', 'openq4_guiAction set_b_system',
        'waitMsec 1500', 'openq4_system report', 'openq4_retainedGui report', 'ui_retainedStatus',
        'screenshot "screenshots/system-optin.tga"',
        'openq4_retainedGui event onTabNext', 'waitMsec 400', 'openq4_retainedGui report',
        'screenshot "screenshots/system-optin-screen.tga"',
        'openq4_retainedGui event onTabNext', 'waitMsec 400', 'openq4_retainedGui report',
        'screenshot "screenshots/system-optin-image.tga"',
        'openq4_retainedGui focus settings_postaa', 'waitMsec 100', 'openq4_retainedGui choice open settings_postaa', 'waitMsec 400',
        'screenshot "screenshots/system-optin-list.tga"', *menu_step('back'),
        'openq4_system back', 'waitMsec 1000', 'openq4_system report',
    ]),
    # Opted into, the Menu button on the SYSTEM page (section 13.5): with the
    # brightness drafted it applies the change; with another drafted, Back asks
    # to discard and Menu there continues editing, as Back does.
    'system-menu-apply': (None, [
        'waitMsec 9000', 'openq4_guiAction main_b_settings', 'waitMsec 1500', 'openq4_guiAction set_b_system',
        'waitMsec 1500', 'openq4_system report', 'r_brightness', 'openq4_retainedGui event tab_image', 'waitMsec 400',
        'openq4_retainedGui focus settings_brightness', 'openq4_retainedGui menu right 1', 'openq4_retainedGui menu right 0',
        'waitMsec 600', 'openq4_retainedGui report', 'screenshot "screenshots/system-menu-pending.tga"',
        'openq4_retainedGui event onMenuButton', 'waitMsec 1500', 'openq4_retainedGui report', 'r_brightness',
        'screenshot "screenshots/system-menu-applied.tga"',
        'openq4_retainedGui focus settings_brightness', 'openq4_retainedGui menu right 1', 'openq4_retainedGui menu right 0',
        'waitMsec 600', 'openq4_system back', 'waitMsec 1000', 'openq4_retainedGui report',
        'screenshot "screenshots/system-menu-discard.tga"',
        'openq4_retainedGui event onMenuButton', 'waitMsec 1000', 'openq4_retainedGui report', 'openq4_system report',
        'screenshot "screenshots/system-menu-editing.tga"',
    ]),
    # Opted into, the multiplayer menu's SYSTEM route (the event the game sends
    # through "main fromMp_toSystem") opens the retained page over the main
    # page, and Back returns to it.
    'system-mp-route': (None, [
        'waitMsec 4000', 'GuiEvent fromMp_toSystem', 'waitMsec 1500', 'openq4_system report', 'ui_retainedStatus',
        'screenshot "screenshots/system-mp-route.tga"', 'openq4_system back', 'waitMsec 1000', 'openq4_system report',
        'openq4_guiGet desktop::dest', 'openq4_guiGet desktop::active', 'openq4_guiGet desktop::curr', 'ui_retainedStatus',
    ]),
    # Opted into, the real route on a listen server: the in-game menu's
    # Settings page and SYSTEM button send "mainmenu fromMp_toSystem", the game
    # returns "main fromMp_toSystem", and the main menu opens the retained page.
    # After Back the main menu must take input again (desktop::active 0).
    'system-mp-game': ('mp:mp/q4dm1', [
        'openq4_assertMenuActivation 4000 game', 'waitMsec 1500', 'openq4_guiAction main_b_settings', 'waitMsec 1500',
        'openq4_guiAction set_b_system', 'waitMsec 3000', 'openq4_system report', 'ui_retainedStatus',
        'screenshot "screenshots/system-mp-game.tga"', 'openq4_system back', 'waitMsec 1500', 'openq4_system report',
        'openq4_guiGet desktop::active', 'openq4_guiGet desktop::curr', 'screenshot "screenshots/system-mp-game-back.tga"',
    ]),
    # Opted into, a SYSTEM page that cannot load: the multiplayer route opens the stock page.
    'fallback-system-mp': (None, [
        'waitMsec 4000', 'GuiEvent fromMp_toSystem', 'waitMsec 2500', 'openq4_guiGet p_settings_sys::visible',
        'openq4_guiGet desktop::active', 'openq4_system report', 'ui_retainedStatus', 'screenshot "screenshots/fallback-system-mp.tga"',
    ]),
    # Opted into, a SYSTEM page that cannot load: the same click opens the stock page.
    'fallback-system': (None, [
        'waitMsec 4000', 'openq4_guiAction main_b_settings', 'waitMsec 1500', 'openq4_guiAction set_b_system',
        'waitMsec 1500', 'openq4_guiGet p_settings_sys::visible', 'openq4_system report', 'ui_retainedStatus',
        'screenshot "screenshots/fallback-system.tga"',
    ]),
    # The Single Player page and its Campaign sub-page (spec 1.10): Campaign
    # leads deeper (the band steps, the title becomes the crumb, the sub-page
    # presents at 350 ms), and Back climbs to the page at 300 ms.
    'subpage': (None, [
        'waitMsec 4000', 'campaignMenu', 'waitMsec 1200', 'screenshot "screenshots/subpage-page.tga"',
        'openq4_retainedGui focus campaign', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 120', 'screenshot "screenshots/subpage-deeper.tga"', 'waitMsec 900', 'ui_retainedStatus',
        'screenshot "screenshots/subpage-sub.tga"', 'openq4_retainedGui focus back', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 200', 'screenshot "screenshots/subpage-back.tga"', 'waitMsec 900',
        'openq4_retainedGui report', 'screenshot "screenshots/subpage-returned.tga"',
    ]),
    # The same change under reduced motion: each level presents at once and
    # its arrival fades in within 80 ms.
    'subpage-reduced': (None, [
        'waitMsec 4000', 'campaignMenu', 'waitMsec 1200', 'openq4_retainedGui focus campaign', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 40', 'screenshot "screenshots/subpage-reduced-deeper.tga"', 'waitMsec 400',
        'screenshot "screenshots/subpage-reduced-sub.tga"', 'openq4_retainedGui focus back', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 400', 'openq4_retainedGui report', 'screenshot "screenshots/subpage-reduced-returned.tga"',
    ]),
    # The Load Game page (FLOW-004): three saves made in the level, then the
    # title, whose CONTINUE describes the newest. LOAD GAME hands off (the
    # bands dock, the label carries) and the page presents at 550 ms with the
    # newest save selected. A row's first Accept selects its save, DELETE GAME
    # asks and the next save takes the deleted one's place, Back returns home
    # with the bands, and Accept on the selected row loads it. In game the
    # page stands over the paused view.
    'loadgame': ('game/airdefense1', [
        'waitMsec 1000', 'saveGame "Trench entry"', 'waitMsec 1500', 'saveGame "Bunker door"', 'waitMsec 1500', 'saveGame Medic',
        'waitMsec 1500', 'disconnect', 'waitMsec 4000', 'ui_retainedStatus', 'screenshot "screenshots/loadgame-title.tga"',
        'openq4_retainedGui focus nav_continue', 'waitMsec 300', 'screenshot "screenshots/loadgame-title-continue.tga"',
        'openq4_retainedGui focus nav_loadgame', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 250', 'screenshot "screenshots/loadgame-handoff.tga"', 'waitMsec 800', 'ui_retainedStatus',
        'openq4_retainedGui report', 'screenshot "screenshots/loadgame.tga"',
        'openq4_retainedGui focus "row-1"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 300',
        'screenshot "screenshots/loadgame-selected.tga"',
        'openq4_retainedGui focus load_delete', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 500',
        'screenshot "screenshots/loadgame-delete.tga"',
        'openq4_retainedGui focus deleteModal_yes', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 600', 'openq4_retainedGui report', 'screenshot "screenshots/loadgame-deleted.tga"',
        'openq4_retainedGui focus load_back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 250',
        'screenshot "screenshots/loadgame-returning.tga"', 'waitMsec 700', 'ui_retainedStatus', 'screenshot "screenshots/loadgame-home.tga"',
        'openq4_retainedGui focus nav_loadgame', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 1000',
        'openq4_retainedGui focus "row-0"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 9000',
        'ui_retainedStatus', 'screenshot "screenshots/loadgame-loaded.tga"',
        'openq4_assertMenuActivation 10000', 'waitMsec 1200', 'openq4_retainedGui focus nav_loadgame', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 250', 'screenshot "screenshots/loadgame-ingame-handoff.tga"', 'waitMsec 800',
        'ui_retainedStatus', 'screenshot "screenshots/loadgame-ingame.tga"',
        'openq4_retainedGui focus load_back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 900',
        'ui_retainedStatus', 'screenshot "screenshots/loadgame-ingame-home.tga"',
    ]),
    # No saves: the empty state says why and, at the title, NEW GAME opens the
    # Single Player page.
    'loadgame-empty': (None, [
        'waitMsec 4000', 'openq4_retainedGui focus nav_loadgame', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 1100', 'ui_retainedStatus', 'openq4_retainedGui report',
        'screenshot "screenshots/loadgame-empty.tga"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1200', 'ui_retainedStatus', 'screenshot "screenshots/loadgame-empty-newgame.tga"',
    ]),
    # No saves and a filter: AUTOSAVES filters the empty list, so its empty
    # state says none matches and offers CLEAR FILTERS where NEW GAME was;
    # CLEAR FILTERS clears the filter and NEW GAME returns.
    'loadgame-empty-filter': (None, [
        'waitMsec 4000', 'openq4_retainedGui focus nav_loadgame', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 1100', 'openq4_retainedGui focus "browser-automatic"',
        'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 600',
        'openq4_retainedGui inspect "load-empty-clear"', 'openq4_retainedGui inspect "load_newgame"',
        'screenshot "screenshots/loadgame-empty-filtered.tga"',
        'openq4_retainedGui focus "load-empty-clear"', 'waitMsec 300', 'screenshot "screenshots/loadgame-empty-clear-focus.tga"',
        'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 600',
        'openq4_retainedGui inspect "load-empty-clear"', 'openq4_retainedGui inspect "load_newgame"',
        'screenshot "screenshots/loadgame-empty-cleared.tga"',
    ]),
    # A save the list accepts but LoadGame refuses (a damaged payload, seeded
    # below): Accept on it says why over its picture and the page stays.
    'loadgame-failure': (None, [
        'waitMsec 4000', 'openq4_retainedGui focus nav_loadgame', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 1100', 'openq4_retainedGui focus "row-0"', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 500', 'ui_retainedStatus', 'openq4_retainedGui report',
        'screenshot "screenshots/loadgame-failure.tga"',
    ]),
    # More saves than the list holds: the older ones are a page away.
    'loadgame-pages': ('game/airdefense1', ['waitMsec 1000'] + [command for index in range(70) for command in (
        f'saveGame page{index:02d}', 'waitMsec 150')] + [
        'disconnect', 'waitMsec 4000', 'openq4_retainedGui focus nav_loadgame', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 1100', 'openq4_retainedGui focus "row-63"', 'waitMsec 400',
        'screenshot "screenshots/loadgame-pages-end.tga"', 'openq4_retainedGui focus "page-older"', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 500', 'openq4_retainedGui report', 'screenshot "screenshots/loadgame-pages-older.tga"',
        'openq4_retainedGui focus "page-newer"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 500',
        'openq4_retainedGui report', 'screenshot "screenshots/loadgame-pages-newer.tga"',
    ]),
    # The Save Game page (FLOW-004): two saves made in the level, then the
    # pause. SAVE GAME hands off as LOAD GAME does and the page arrives with no
    # name, the field focused. A row fills the name; a typed name, entered,
    # saves at once and becomes the newest; SAVE GAME over a name a save holds
    # asks first and YES saves over it; an autosave (seeded below) is refused;
    # Back returns the pause. Typing runs through the field's own editor
    # operations, never a device.
    'savegame': ('game/airdefense1', [
        'waitMsec 1000', 'saveGame "Trench entry"', 'waitMsec 1500', 'saveGame "Bunker door"', 'waitMsec 1500',
        'openq4_assertMenuActivation 10000', 'waitMsec 1200', 'ui_retainedStatus',
        'openq4_retainedGui focus nav_savegame', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 250', 'screenshot "screenshots/savegame-handoff.tga"', 'waitMsec 800', 'ui_retainedStatus',
        'openq4_retainedGui report', 'screenshot "screenshots/savegame.tga"',
        'openq4_retainedGui focus "row-1"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 300',
        'openq4_retainedGui widget "save-name"', 'screenshot "screenshots/savegame-selected.tga"',
        'openq4_retainedGui focus "save-name"', 'openq4_retainedGui number begin "save-name"',
        'openq4_retainedGui number input "save-name" "Medic station"', 'waitMsec 300', 'openq4_retainedGui widget "save-name"',
        'screenshot "screenshots/savegame-typing.tga"',
        'openq4_retainedGui number commit "save-name"', 'waitMsec 1500', 'openq4_retainedGui report',
        'screenshot "screenshots/savegame-saved.tga"',
        'openq4_retainedGui focus save_go', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 500',
        'screenshot "screenshots/savegame-overwrite.tga"',
        'openq4_retainedGui focus overwriteModal_yes', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1500', 'openq4_retainedGui report', 'screenshot "screenshots/savegame-overwritten.tga"',
        'openq4_retainedGui focus "save-name"', 'openq4_retainedGui number begin "save-name"',
        'openq4_retainedGui number input "save-name" "auto"', 'openq4_retainedGui number commit "save-name"', 'waitMsec 600',
        'screenshot "screenshots/savegame-autosave.tga"',
        'openq4_retainedGui focus noticeModal_close', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 500', 'openq4_retainedGui focus save_back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 900', 'ui_retainedStatus', 'screenshot "screenshots/savegame-home.tga"',
    ]),
    # A level without saves: the page's empty list, the hint in the empty
    # field and SAVE GAME resting until a name is typed.
    'savegame-empty': ('game/airdefense1', [
        'waitMsec 1500', 'openq4_assertMenuActivation 10000', 'waitMsec 1200', 'openq4_retainedGui focus nav_savegame',
        'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 1100', 'ui_retainedStatus',
        'openq4_retainedGui report', 'screenshot "screenshots/savegame-empty.tga"',
        'openq4_retainedGui number begin "save-name"', 'waitMsec 300', 'screenshot "screenshots/savegame-empty-editing.tga"',
    ]),
    # Single Player's Arena sub-page (FLOW-011, FLOW-050): ARENA plays the
    # change with its own carry and the campaign's browser presents at 350 ms,
    # TIER 1 won and TIER 2's first qualifier with it (ui_arenaProgress 31).
    # The bumpers' event moves to TIER 3 (locked: nothing moves) and back, a
    # qualifier is chosen, the difficulty steps, RESET PROGRESS asks first and
    # CANCEL keeps the progress; Back returns to Single Player onto ARENA.
    'arena': (None, [
        'set ui_arenaProgress 31', 'set ui_arenaSelectedTier 1', 'set ui_arenaSelectedMatch 1', 'set ui_arenaDifficulty 3',
        'waitMsec 4000', 'campaignMenu', 'waitMsec 1200', 'screenshot "screenshots/arena-singleplayer.tga"',
        'openq4_retainedGui focus arena', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 150', 'screenshot "screenshots/arena-change.tga"', 'waitMsec 1100', 'ui_retainedStatus', 'openq4_retainedGui report',
        'screenshot "screenshots/arena.tga"',
        'openq4_retainedGui event onTabNext', 'waitMsec 400', 'ui_arenaSelectedTier', 'screenshot "screenshots/arena-tier-locked.tga"',
        'openq4_retainedGui event onTabPrevious', 'waitMsec 400', 'ui_arenaSelectedTier', 'screenshot "screenshots/arena-tier-1.tga"',
        'openq4_retainedGui event onTabNext', 'waitMsec 400', 'ui_arenaSelectedTier',
        'openq4_retainedGui focus "match-2"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 400',
        'ui_arenaSelectedMatch', 'screenshot "screenshots/arena-match.tga"',
        'openq4_retainedGui focus arena_harder', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 300',
        'ui_arenaDifficulty', 'screenshot "screenshots/arena-harder.tga"',
        'openq4_retainedGui focus arena_reset', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 450',
        'openq4_retainedGui report', 'screenshot "screenshots/arena-reset.tga"',
        'openq4_retainedGui focus resetModal_no', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 450',
        'ui_arenaProgress', 'screenshot "screenshots/arena-kept.tga"',
        'openq4_retainedGui focus arena_back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 150', 'screenshot "screenshots/arena-back.tga"', 'waitMsec 900', 'openq4_retainedGui report',
        'screenshot "screenshots/arena-returned.tga"',
    ]),
    # An Arena match from the page: START MATCH plays the entrance and the
    # campaign launches the first match in game_mp with its bot. Disconnecting
    # abandons it: the campaign aborts the match, restores its settings and
    # keeps its progress. The reloaded module logs to its own file.
    # The Arena match's own pause (section 13.7): the game's Arena pause menu
    # under its retained card, then RESUME back to the match.
    'arenapause': (None, [
        'set ui_arenaProgress 0', 'set ui_arenaSelectedTier 0', 'set ui_arenaSelectedMatch 0', 'set ui_arenaDifficulty 3',
        'waitMsec 4000', 'campaignMenu', 'waitMsec 1200',
        'openq4_retainedGui focus arena', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2500', 'set logFileName "logs/openq4-match.log"',
        'openq4_retainedGui focus arena_start', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 400', AFTER_MAP,
        'openq4_assertMenuActivation 4000 game', 'waitMsec 1500', 'ui_retainedStatus', 'openq4_retainedGui report',
        'screenshot "screenshots/arenapause.tga"',
        'openq4_retainedGui focus nav_resume', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1000', 'ui_retainedStatus', 'screenshot "screenshots/arenapause-resumed.tga"',
    ]),
    'arenamatch': (None, [
        'set ui_arenaProgress 0', 'set ui_arenaSelectedTier 0', 'set ui_arenaSelectedMatch 0', 'set ui_arenaDifficulty 3',
        'waitMsec 4000', 'campaignMenu', 'waitMsec 1200', 'openq4_retainedGui focus arena', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 2500', 'screenshot "screenshots/arenamatch-browser.tga"',
        'set logFileName "logs/openq4-match.log"', 'openq4_retainedGui focus arena_start', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 400', 'screenshot "screenshots/arenamatch-entrance.tga"', AFTER_MAP,
        'ui_retainedStatus', 'ui_arenaCampaignActive', 'ui_arenaActiveMatch', 'screenshot "screenshots/arenamatch-match.tga"',
        'disconnect', 'waitMsec 3000', 'ui_arenaCampaignActive', 'ui_arenaActiveMatch', 'ui_arenaProgress', 'si_arenaCampaign',
        'ui_retainedStatus', 'screenshot "screenshots/arenamatch-left.tga"',
    ]),
    # An Arena match that cannot start: the listen server's port is held
    # (SCENARIO_PORTS), so after the entrance and the game module's reload the
    # server cannot open and the campaign returns to the page with its result
    # ledger, MATCH ABORTED and the reason; CONTINUE returns to the browser
    # with the progress untouched. The campaign appends its launch behind the
    # script, so the rest follows it through two appended steps
    # (SCENARIO_FILES: vstr appends, where exec would insert; the console
    # splits a bare name at a hyphen, so they have none). The log of the
    # reloaded module goes to its own file, keeping the page's.
    'arenafail': (None, [
        'set ui_arenaProgress 0', 'set ui_arenaSelectedTier 0', 'set ui_arenaSelectedMatch 0', 'set ui_arenaDifficulty 3',
        'set arenaDefer "exec retained_arenafail_defer.cfg"', 'set arenaAfter "exec retained_arenafail_after.cfg"',
        'waitMsec 4000', 'campaignMenu', 'waitMsec 1200', 'openq4_retainedGui focus arena', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 2500', 'set logFileName "logs/openq4-match.log"',
        'openq4_retainedGui focus arena_start', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 400', 'screenshot "screenshots/arenafail-entrance.tga"', 'waitMsec 1500',
        'vstr arenaDefer', AFTER_MAP,
    ]),
    # A whole Arena match to its result: the hardest difficulty's bot plays the
    # first match out against the idle player, and the page reveals the
    # campaign's result ledger from the black the match ended on.
    'arenaresult': (None, [
        'set ui_arenaProgress 0', 'set ui_arenaSelectedTier 0', 'set ui_arenaSelectedMatch 0', 'set ui_arenaDifficulty 5',
        'waitMsec 4000', 'campaignMenu', 'waitMsec 1200', 'openq4_retainedGui focus arena', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 1300', 'openq4_retainedGui focus arena_start', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 400', AFTER_MAP,
        'screenshot "screenshots/arenaresult-match.tga"', 'waitMsec 120000', 'screenshot "screenshots/arenaresult-2min.tga"',
        'waitMsec 120000', 'screenshot "screenshots/arenaresult-4min.tga"', 'waitMsec 120000', 'screenshot "screenshots/arenaresult-6min.tga"',
        'waitMsec 60000', 'openq4_retainedGui report', 'screenshot "screenshots/arenaresult-7min.tga"',
        'waitMsec 60000', 'openq4_retainedGui report', 'screenshot "screenshots/arenaresult-8min.tga"',
    ]),
    # The Multiplayer page (FLOW-050): MULTIPLAYER hands off and the page
    # presents at 550 ms in the page state under its carried title. CREATE
    # SERVER and JOIN GAME go deeper to their retained sub-pages (JOIN GAME's
    # on the LAN source, openq4_browser source lan, so no list asks a master
    # server), and each one's BACK climbs out onto its section; PLAYER SETUP
    # leaves the page as the stock Multiplayer page leaves, the stock Settings
    # page takes over at 500 ms and its BACK returns the retained page; BACK
    # returns the title from the page dock.
    'multiplayer': (None, [
        'waitMsec 7000', 'openq4_browser source lan',
        'openq4_retainedGui focus nav_multiplayer', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 250', 'screenshot "screenshots/multiplayer-handoff.tga"', 'waitMsec 1000', 'ui_retainedStatus',
        'openq4_retainedGui report', 'screenshot "screenshots/multiplayer.tga"',
        'openq4_retainedGui focus create', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 150', 'screenshot "screenshots/multiplayer-depart.tga"', 'waitMsec 1500', 'ui_retainedStatus',
        'screenshot "screenshots/multiplayer-create.tga"',
        'openq4_retainedGui focus create_back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1500', 'ui_retainedStatus', 'openq4_retainedGui report',
        'screenshot "screenshots/multiplayer-returned.tga"',
        'openq4_retainedGui focus join', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1500', 'screenshot "screenshots/multiplayer-browser.tga"',
        'openq4_retainedGui focus join_back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1500', 'openq4_retainedGui report',
        'openq4_retainedGui focus setup', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1500', 'screenshot "screenshots/multiplayer-setup.tga"',
        'openq4_retainedGui focus "setup-close"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1500', 'openq4_retainedGui report',
        'openq4_retainedGui focus back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 250', 'screenshot "screenshots/multiplayer-back.tga"', 'waitMsec 1000', 'ui_retainedStatus',
        'screenshot "screenshots/multiplayer-title.tga"',
    ]),
    # Player Setup edits in place on the Multiplayer page (FLOW-050): the
    # sections give way to the editor and its model preview. A typed name,
    # the next model, a rail colour and a turn of the preview apply at once
    # (each CVar read back), and CLOSE returns the sections with the focus on
    # PLAYER SETUP.
    'playersetup': (None, [
        'waitMsec 7000',
        'openq4_retainedGui focus nav_multiplayer', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1500', 'openq4_retainedGui focus setup', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2000', 'ui_retainedStatus', 'openq4_retainedGui report', 'screenshot "screenshots/playersetup.tga"',
        'openq4_retainedGui focus "setup-name"', 'openq4_retainedGui number begin "setup-name"',
        'openq4_retainedGui number input "setup-name" "Kane"', 'openq4_retainedGui number commit "setup-name"', 'waitMsec 600', 'ui_name',
        'openq4_retainedGui focus "setup-model"', 'waitMsec 100', 'openq4_retainedGui choice open "setup-model"', 'waitMsec 500',
        'screenshot "screenshots/playersetup-models.tga"',
        'openq4_retainedGui menu down 1', 'openq4_retainedGui menu down 0', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 2000', 'ui_model', 'openq4_retainedGui report',
        'screenshot "screenshots/playersetup-model.tga"',
        'openq4_retainedGui focus "setup-rail-3"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 600', 'ui_hitscanTint',
        'openq4_retainedGui focus "setup-rotation"', 'openq4_retainedGui menu right 1', 'openq4_retainedGui menu right 0',
        'openq4_retainedGui menu right 1', 'openq4_retainedGui menu right 0', 'waitMsec 800', 'openq4_retainedGui report',
        'screenshot "screenshots/playersetup-turned.tga"',
        'openq4_retainedGui focus "setup-close"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1200', 'openq4_retainedGui report', 'screenshot "screenshots/playersetup-closed.tga"',
    ]),
    # The right stick turns Player Setup's preview (section 13.5) through the
    # session, which owns the Rotation row: held right for 500 ms it turns
    # 200 to 290 degrees, held half left for two seconds back to 110, and the
    # row's slider follows. Player Setup's rows are controls, so there is no
    # detail area for up and down to scroll.
    'playersetup-stick': (None, [
        'waitMsec 7000',
        'openq4_retainedGui focus nav_multiplayer', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1500', 'openq4_retainedGui focus setup', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2000', 'openq4_retainedGui widget "setup-rotation"', 'screenshot "screenshots/playersetup-stick-start.tga"',
        'openq4_retainedGui stick 1 0 500', 'waitMsec 400', 'openq4_retainedGui widget "setup-rotation"',
        'screenshot "screenshots/playersetup-stick-right.tga"',
        'openq4_retainedGui stick -0.5 0 2000', 'waitMsec 400', 'openq4_retainedGui widget "setup-rotation"',
        'screenshot "screenshots/playersetup-stick-left.tga"',
        'openq4_retainedGui stick 0 1 500',
        'openq4_retainedGui focus "setup-close"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1200',
    ]),
    # The Create Server sub-page (FLOW-050), the default: CREATE SERVER on the
    # Multiplayer page goes deeper and the sub-page presents at 350 ms under
    # the MULTIPLAYER crumb, the game type's maps listed and the chosen one's
    # levelshot shown. Choosing CTF lists its maps again, ADVANCED OPTIONS
    # opens the popup's options (a password typed, the seeded ban list opened
    # and one ban removed), and BACK climbs out onto CREATE SERVER.
    'createserver': (None, [
        'waitMsec 4000',
        'openq4_retainedGui focus nav_multiplayer', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1200', 'openq4_retainedGui report',
        'openq4_retainedGui focus create', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 175', 'screenshot "screenshots/createserver-change.tga"', 'waitMsec 1200', 'ui_retainedStatus',
        'openq4_retainedGui report', 'screenshot "screenshots/createserver.tga"',
        'openq4_retainedGui focus "create-gametype"', 'waitMsec 100', 'openq4_retainedGui choice open "create-gametype"', 'waitMsec 400',
        'screenshot "screenshots/createserver-gametypes.tga"',
        'openq4_retainedGui menu down 1', 'openq4_retainedGui menu down 0', 'openq4_retainedGui menu down 1', 'openq4_retainedGui menu down 0',
        'openq4_retainedGui menu down 1', 'openq4_retainedGui menu down 0',
        'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 600', 'si_gameType', 'si_map',
        'screenshot "screenshots/createserver-ctf.tga"',
        'openq4_retainedGui event advancedModalShow', 'waitMsec 500', 'openq4_retainedGui report',
        'screenshot "screenshots/createserver-advanced.tga"',
        # Use password unlocks Set password; a typed password shows as its
        # mask while editing and once the session has taken it.
        'openq4_retainedGui focus "create-usepass"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 300', 'openq4_retainedGui focus "create-password"', 'openq4_retainedGui number begin "create-password"',
        'openq4_retainedGui number input "create-password" "hunter2"', 'waitMsec 300',
        'screenshot "screenshots/createserver-password.tga"',
        'openq4_retainedGui number commit "create-password"', 'waitMsec 300', 'si_usePass', 'g_password',
        'openq4_retainedGui widget "create-password"', 'screenshot "screenshots/createserver-password-set.tga"',
        # VIEW LOCAL SERVER BAN LIST opens the seeded bans over the dialog; the
        # second is chosen and removed, and CLOSE returns to its row.
        'openq4_retainedGui focus "create-banlist"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 500', 'openq4_retainedGui report', 'screenshot "screenshots/createserver-banlist.tga"',
        'openq4_retainedGui focus "ban-row-1"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 300',
        'openq4_retainedGui focus banModal_yes', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 300',
        'screenshot "screenshots/createserver-banlist-removed.tga"',
        'openq4_retainedGui focus banModal_no', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 600',
        'openq4_retainedGui report',
        'openq4_retainedGui event advancedModalHide', 'waitMsec 600',
        'openq4_retainedGui focus create_back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 150', 'screenshot "screenshots/createserver-back.tga"', 'waitMsec 1200', 'openq4_retainedGui report',
        'screenshot "screenshots/createserver-returned.tga"',
    ]),
    # CREATE SERVER from the sub-page starts the server as the stock page's
    # does: the menu closes, the game module reloads into game_mp and the
    # listen server loads the chosen map in the chosen game type (Team DM on
    # its first map here). It listens on the loopback only (SCENARIO_CVARS),
    # and the reloaded module logs to its own file.
    'createserverstart': (None, [
        'waitMsec 4000',
        'openq4_retainedGui focus nav_multiplayer', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1200',
        'openq4_retainedGui focus create', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1400', 'openq4_retainedGui focus "create-gametype"', 'waitMsec 100', 'openq4_retainedGui choice open "create-gametype"',
        'waitMsec 400', 'openq4_retainedGui menu down 1', 'openq4_retainedGui menu down 0', 'openq4_retainedGui menu down 1',
        'openq4_retainedGui menu down 0', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 600',
        'si_gameType', 'si_map', 'screenshot "screenshots/createserverstart-teamdm.tga"',
        'set logFileName "logs/openq4-match.log"',
        'openq4_retainedGui focus create_start', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', AFTER_MAP,
        'si_gameType', 'si_map', 'ui_retainedStatus', 'screenshot "screenshots/createserverstart-match.tga"', 'disconnect', 'waitMsec 2000',
    ]),
    # The Join Game scenarios wait 2.5 s around page and dialog changes: a
    # hidden Vulkan window can wait up to a second for a swapchain image, so
    # its frames alternate ~40 ms and ~1 s, and a dialog's focus lands only on
    # the frame its timeline ends.
    # The Join Game sub-page (FLOW-050), the default: JOIN GAME
    # on the Multiplayer page goes deeper and the browser presents at 350 ms
    # under the MULTIPLAYER crumb, empty before any scan. INTERNET gets a new
    # list: the master server cannot be reached from the loopback the client
    # binds, but the seeded favorite, a loopback dedicated server
    # (SCENARIO_SERVERS), answers and is listed. SERVER NAME sorts the list
    # and CLEAR SORTING clears the sort; a controller's north sorts by PING and
    # its west asks for the list again; BACK climbs out onto JOIN GAME and
    # JOIN GAME returns to the list; the row chooses the server and JOIN
    # SERVER joins it, the game module reloading into game_mp (logged to its
    # own file). Before the join the chosen server shows its levelshot and
    # dedicated symbol, the favorite toggle takes it off the favorites and
    # puts it back, and MOD's spinner steps to the next mod and back.
    # The View button hides and shows Join Game's server details (section
    # 13.5): the focused row's card, then hidden, then back. The log first
    # reports View's default bind in play.
    'join-details-view': (None, [
        'waitMsec 7000', 'bind JOY8',
        'openq4_retainedGui focus nav_multiplayer', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2500', 'openq4_retainedGui focus join', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2700', 'openq4_retainedGui focus join_internet', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 4800', 'openq4_retainedGui focus "server-0"', 'waitMsec 2500',
        'openq4_retainedGui inspect "join-detail"', 'screenshot "screenshots/join-details-shown.tga"',
        'openq4_retainedGui event onViewButton', 'waitMsec 400', 'openq4_retainedGui inspect "join-detail"',
        'screenshot "screenshots/join-details-hidden.tga"',
        'openq4_retainedGui event onViewButton', 'waitMsec 400', 'openq4_retainedGui inspect "join-detail"',
        'screenshot "screenshots/join-details-again.tga"',
    ]),
    # Each controller family's View button at the card's corner (section
    # 13.5); the keyboard shows none.
    'join-view-glyphs': (None, [
        'waitMsec 7000',
        'openq4_retainedGui focus nav_multiplayer', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2500', 'openq4_retainedGui focus join', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2700', 'openq4_retainedGui focus join_internet', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 4800', 'openq4_retainedGui focus "server-0"', 'waitMsec 2500',
        *[step for style, name in PROMPT_STYLES for step in (
            *prompt_style(style), 'waitMsec 500', 'openq4_retainedGui inspect "join-detail-view"',
            f'screenshot "screenshots/join-view-{name}.tga"')],
        *prompt_style(0),
    ]),
    # SPECTATE between JOIN SERVER and the favorite toggle, and each
    # controller family's stick presses past them (section 13.5: the left
    # stick spectates, the right one toggles the favorite); the keyboard
    # shows none. No scan is needed: the glyphs stand with the plates.
    'join-stick-glyphs': (None, [
        'waitMsec 7000',
        'openq4_retainedGui focus nav_multiplayer', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2500', 'openq4_retainedGui focus join', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2700', 'openq4_retainedGui inspect "join_spectate"',
        *[step for style, name in PROMPT_STYLES for step in (
            *prompt_style(style), 'waitMsec 500', 'openq4_retainedGui inspect "join_spectate-stick"',
            f'screenshot "screenshots/join-sticks-{name}.tga"')],
        *prompt_style(0),
    ]),
    'joingame': (None, [
        # A fresh build's first start can run past the usual wait; the title
        # refuses focus until it has arrived.
        'waitMsec 7000',
        'openq4_retainedGui focus nav_multiplayer', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2500', 'openq4_retainedGui report',
        'openq4_retainedGui focus join', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 175', 'screenshot "screenshots/joingame-change.tga"', 'waitMsec 2500', 'ui_retainedStatus',
        'openq4_retainedGui report', 'openq4_retainedGui inspect "join-empty"', 'screenshot "screenshots/joingame-empty.tga"',
        'openq4_retainedGui focus join_internet', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 300', 'openq4_retainedGui inspect "join-loading"', 'screenshot "screenshots/joingame-scanning.tga"', 'waitMsec 4500', 'openq4_retainedGui report',
        'openq4_retainedGui inspect "server-0"', 'openq4_retainedGui inspect "server-1"', 'openq4_retainedGui inspect "join-empty"',
        'openq4_retainedGui inspect "join-load-track"', 'openq4_retainedGui inspect "join-load-fill"',
        'openq4_retainedGui inspect "server-0-bar-1"', 'openq4_retainedGui inspect "server-0-bar-4"', 'openq4_retainedGui inspect "server-0-ping"',
        'screenshot "screenshots/joingame-list.tga"',
        # The focused row's server details: the card under the row.
        'openq4_retainedGui focus "server-0"', 'waitMsec 2500', 'openq4_retainedGui inspect "server-0"',
        'openq4_retainedGui inspect "join-detail"', 'screenshot "screenshots/joingame-detail.tga"',
        'openq4_retainedGui focus "heading-name"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 400', 'openq4_retainedGui inspect "heading-name-up"', 'screenshot "screenshots/joingame-sorted.tga"',
        'openq4_retainedGui focus join_clearsort', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 400', 'openq4_retainedGui inspect "heading-name-up"',
        # A controller's north sorts by the next column, PING from none, and
        # its west asks for the list again.
        'openq4_retainedGui event onTertiary', 'waitMsec 400', 'openq4_retainedGui inspect "heading-ping-up"',
        'openq4_retainedGui event onSecondary', 'waitMsec 2500', 'openq4_retainedGui report', 'openq4_retainedGui inspect "server-0"',
        'screenshot "screenshots/joingame-controller.tga"',
        'openq4_retainedGui focus join_back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2500', 'openq4_retainedGui report', 'screenshot "screenshots/joingame-back.tga"',
        'openq4_retainedGui focus join', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2500', 'openq4_retainedGui focus "server-0"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 400', 'openq4_retainedGui report', 'openq4_retainedGui inspect "server-0-selected"',
        'openq4_retainedGui inspect "server-0-ded"', 'openq4_retainedGui inspect "join-shot"',
        'screenshot "screenshots/joingame-chosen.tga"',
        # The favorite toggle takes the server off the favorites and puts it
        # back; MOD's spinner shows the next mod and the previous again.
        'openq4_retainedGui focus join_favorite', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 400', 'openq4_retainedGui inspect "server-0-fav"', 'screenshot "screenshots/joingame-unfavorite.tga"',
        'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 400', 'openq4_retainedGui inspect "server-0-fav"',
        'openq4_retainedGui focus join_mod_next', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 400', 'openq4_retainedGui inspect "server-0"', 'screenshot "screenshots/joingame-mod.tga"',
        'openq4_retainedGui focus join_mod_previous', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 400',
        # FIND PLAYER/CLAN for a name no player has: the list empties under the
        # search's tag, and the tag's Accept brings the server back.
        'openq4_retainedGui focus join_find', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 2500',
        'openq4_retainedGui report', 'openq4_retainedGui number begin "join-find"', 'openq4_retainedGui number input "join-find" "Grunt"',
        'openq4_retainedGui number commit "join-find"', 'waitMsec 2500', 'openq4_retainedGui inspect "server-0"',
        'openq4_retainedGui inspect "join_search"', 'screenshot "screenshots/joingame-search.tga"',
        'openq4_retainedGui focus join_search', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 1000',
        'openq4_retainedGui inspect "server-0"', 'openq4_retainedGui inspect "join_search"',
        'set logFileName "logs/openq4-match.log"',
        'openq4_retainedGui focus join_server', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', AFTER_MAP,
        'si_map', 'ui_retainedStatus', 'screenshot "screenshots/joingame-match.tga"', 'disconnect', 'waitMsec 2000',
    ]),
    # The Join Game sub-page's filter row (section 7's filter tags): the
    # seeded favorite, the loopback dedicated server (SCENARIO_SERVERS: Deathmatch,
    # nobody playing, no password), is listed; NOT EMPTY hides it and says no
    # server matches, and again brings it back; the game type's spinner shows
    # Deathmatch, which keeps it, then Tourney, which hides it, and back to
    # every type; NOT FULL and NO PASSWORD keep it and light their tags, and
    # again clear them.
    'joinfilters': (None, [
        # A loaded machine's first start can take longer than joingame's wait.
        'waitMsec 12000',
        'openq4_retainedGui focus nav_multiplayer', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2500', 'openq4_retainedGui focus join', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2500', 'openq4_retainedGui focus join_internet', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 4800', 'gui_filter_players', 'gui_filter_password', 'gui_filter_gameType', 'openq4_retainedGui report',
        'openq4_retainedGui inspect "server-0"', 'openq4_retainedGui inspect "join-gametype-value"',
        'openq4_retainedGui inspect "join_filter_empty-lit"', 'screenshot "screenshots/joinfilters-list.tga"',
        'openq4_retainedGui focus join_filter_empty', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 600', 'gui_filter_players', 'openq4_retainedGui inspect "server-0"', 'openq4_retainedGui inspect "join-empty"',
        'openq4_retainedGui inspect "join_filter_empty-lit"', 'openq4_retainedGui inspect "join_filter_empty-cross"',
        'screenshot "screenshots/joinfilters-notempty.tga"',
        'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 600', 'gui_filter_players',
        'openq4_retainedGui inspect "server-0"',
        'openq4_retainedGui focus join_gametype_next', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 600', 'gui_filter_gameType', 'openq4_retainedGui inspect "join-gametype-value"', 'openq4_retainedGui inspect "server-0"',
        'screenshot "screenshots/joinfilters-deathmatch.tga"',
        'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 600', 'gui_filter_gameType',
        'openq4_retainedGui inspect "join-gametype-value"', 'openq4_retainedGui inspect "server-0"', 'openq4_retainedGui inspect "join-empty"',
        'screenshot "screenshots/joinfilters-tourney.tga"',
        'openq4_retainedGui focus join_gametype_previous', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 300', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 600', 'gui_filter_gameType',
        'openq4_retainedGui inspect "join-gametype-value"', 'openq4_retainedGui inspect "server-0"',
        'openq4_retainedGui focus join_filter_full', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 300',
        'openq4_retainedGui focus join_filter_password', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 600', 'gui_filter_players', 'gui_filter_password', 'openq4_retainedGui inspect "server-0"',
        'openq4_retainedGui inspect "join_filter_full-lit"', 'openq4_retainedGui inspect "join_filter_password-lit"',
        'screenshot "screenshots/joinfilters-set.tga"',
        'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 300',
        'openq4_retainedGui focus join_filter_full', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 600', 'gui_filter_players', 'gui_filter_password', 'openq4_retainedGui inspect "join_filter_full-lit"',
        'openq4_retainedGui report',
    ]),
    # The session's notice layer over the retained menus (section 13.13): a
    # controller's loss (section 13.7; testControllerLoss stands in for the
    # device, touching none) shows on the title, stays as the Load Game page
    # takes over, and leaves when a controller returns.
    'notices-controller': (None, [
        'waitMsec 6000', 'testControllerLoss 1', 'waitMsec 600', 'openq4_retainedGui inspect "notice-0"', 'openq4_retainedGui inspect "notice-0-symbol-2"', 'screenshot "screenshots/notice-controller-title.tga"',
        'openq4_retainedGui focus nav_loadgame', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1500', 'ui_retainedStatus', 'openq4_retainedGui inspect "notice-0"', 'openq4_retainedGui inspect "notice-0-symbol-2"', 'screenshot "screenshots/notice-controller-page.tga"',
        'waitMsec 6000', 'openq4_retainedGui inspect "notice-0"', 'openq4_retainedGui inspect "notice-0-symbol-2"', 'screenshot "screenshots/notice-controller-held.tga"',
        'testControllerLoss 0', 'waitMsec 1500', 'openq4_retainedGui inspect "notice-0"', 'openq4_retainedGui inspect "notice-0-symbol-2"', 'screenshot "screenshots/notice-controller-resolved.tga"',
    ]),
    # The retained AUDIO page (UI-01): the settings screen's AUDIO button opens
    # it on the session's values; a step right raises the music; the device
    # row unfolds the sound system's list; Back returns to the settings screen.
    'audio': (None, [
        'waitMsec 4000', 'openq4_guiAction main_b_settings', 'waitMsec 1500', 'openq4_guiAction set_b_audio',
        'waitMsec 1500', 'ui_retainedStatus', 'openq4_retainedGui report', 'screenshot "screenshots/audio.tga"',
        's_musicVolume', 'openq4_retainedGui focus audio_music', 'openq4_retainedGui menu right 1', 'openq4_retainedGui menu right 0',
        'waitMsec 600', 's_musicVolume', 'openq4_retainedGui report', 'screenshot "screenshots/audio-music.tga"',
        'openq4_retainedGui focus audio_device', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 600', 'openq4_retainedGui report', 'screenshot "screenshots/audio-devices.tga"',
        'openq4_retainedGui menu back 1', 'openq4_retainedGui menu back 0', 'waitMsec 400',
        'openq4_retainedGui focus audio_subtitle_size', 'waitMsec 400', 'openq4_retainedGui report',
        'screenshot "screenshots/audio-subtitles.tga"',
        'openq4_retainedGui event onBack', 'waitMsec 1500', 'ui_retainedStatus', 'openq4_guiGet p_settings_audio::visible',
        'screenshot "screenshots/audio-back.tga"',
    ]),
    # The retained GAME OPTIONS page (UI-01): the GAME OPTIONS button opens it
    # on GENERAL's first row; the HUD tab's custom crosshair turns on and steps
    # to the next one; the gun position and a mouse row apply; the language
    # list switches to German, which reloads the language and the menus; Back
    # returns to the settings screen.
    'game': (None, [
        'waitMsec 4000', 'openq4_guiAction main_b_settings', 'waitMsec 1500', 'openq4_guiAction set_b_gameoptions',
        'waitMsec 1500', 'ui_retainedStatus', 'openq4_retainedGui report', 'screenshot "screenshots/game.tga"',
        'openq4_retainedGui event tab_hud', 'waitMsec 600', 'openq4_retainedGui report', 'screenshot "screenshots/game-section.tga"',
        'g_crosshairCustom', 'openq4_retainedGui focus game_customcrosshair', *menu_step('accept'), *menu_step('down'),
        *menu_step('accept'), 'g_crosshairCustom', 'openq4_retainedGui focus game_crosshair', *menu_step('right'),
        'g_crosshairCustomFile', 'waitMsec 400', 'screenshot "screenshots/game-crosshair.tga"',
        'openq4_retainedGui event tab_general', 'waitMsec 400',
        'openq4_retainedGui focus game_gunposition', *menu_step('accept'), *menu_step('down'), *menu_step('accept'), 'g_gunY',
        'openq4_retainedGui event tab_mouse', 'waitMsec 400',
        'sensitivity', 'openq4_retainedGui focus game_msensitivity', *menu_step('right'), 'sensitivity',
        'screenshot "screenshots/game-mouse.tga"',
        'openq4_retainedGui event tab_general', 'waitMsec 400',
        'openq4_retainedGui focus game_language', *menu_step('accept'), *[step for _ in range(6) for step in menu_step('down')],
        *menu_step('accept'), 'waitMsec 3000', 'sys_lang', 'openq4_retainedGui report', 'ui_retainedStatus',
        'screenshot "screenshots/game-language.tga"',
        'openq4_retainedGui event onBack', 'waitMsec 1500', 'ui_retainedStatus', 'openq4_guiGet p_settings_game::visible',
        'screenshot "screenshots/game-back.tga"',
    ]),
    # The multiplayer menu's GAME OPTIONS route (the event the game sends
    # through "main fromMp_toGameoptions") opens the retained page over the
    # main page, and Back returns to it.
    'game-mp-route': (None, [
        'waitMsec 4000', 'GuiEvent fromMp_toGameoptions', 'waitMsec 1500', 'openq4_retainedGui report', 'ui_retainedStatus',
        'screenshot "screenshots/game-mp-route.tga"', 'openq4_retainedGui event onBack', 'waitMsec 1000', 'openq4_retainedGui report',
        'openq4_guiGet desktop::dest', 'openq4_guiGet desktop::active', 'openq4_guiGet desktop::curr', 'ui_retainedStatus',
    ]),
    # The real route on a listen server: the in-game menu's Settings page and
    # GAME OPTIONS button send "mainmenu fromMp_toGameoptions", the game
    # returns "main fromMp_toGameoptions", and the main menu opens the
    # retained page. After Back the main menu must take input again.
    'game-mp-game': ('mp:mp/q4dm1', [
        'openq4_assertMenuActivation 4000 game', 'waitMsec 1500', 'openq4_guiAction main_b_settings', 'waitMsec 1500',
        'openq4_guiAction set_b_gameopts', 'waitMsec 3000', 'openq4_retainedGui report', 'ui_retainedStatus',
        'screenshot "screenshots/game-mp-game.tga"', 'openq4_retainedGui event onBack', 'waitMsec 1500', 'openq4_retainedGui report',
        'openq4_guiGet desktop::active', 'openq4_guiGet desktop::curr', 'screenshot "screenshots/game-mp-game-back.tga"',
    ]),
    # The retained CONTROLS page (UI-01): the CONTROLS button opens the page on Move
    # Forward; Accept waits for a key and testControlsKey stands in for F,
    # which joins the row's keys; a second wait ends with Escape, binding
    # nothing; Delete clears the row; Back returns to the settings screen.
    'controls': (None, [
        'waitMsec 4000', 'openq4_guiAction main_b_settings', 'waitMsec 1500', 'openq4_guiAction set_b_controls',
        'waitMsec 1500', 'ui_retainedStatus', 'openq4_retainedGui report', 'screenshot "screenshots/controls.tga"',
        *menu_step('accept'), 'waitMsec 300', 'screenshot "screenshots/controls-waiting.tga"',
        'testControlsKey f', 'waitMsec 400', 'bind f', 'screenshot "screenshots/controls-bound.tga"',
        *menu_step('accept'), 'testControlsKey ESCAPE', 'waitMsec 300', 'bind ESCAPE', 'bind f',
        'testControlsKey DEL', 'waitMsec 400', 'bind f', 'bind w', 'screenshot "screenshots/controls-cleared.tga"',
        'openq4_retainedGui event tab_other', 'waitMsec 400',
        'openq4_retainedGui focus controls_bind_48', 'waitMsec 400', 'screenshot "screenshots/controls-other.tga"',
        'openq4_retainedGui event onBack', 'waitMsec 1500', 'ui_retainedStatus', 'openq4_guiGet p_settings_ctrls::visible',
        'screenshot "screenshots/controls-back.tga"',
    ]),
    # The retained SETTINGS page (UI-01): SETTINGS on the title hands off to
    # it; CONTROLS opens its page, whose Back lands on CONTROLS; DISPLAY (the
    # SYSTEM page off) sends it away to the stock settings screen on that
    # panel, whose Back returns it onto DISPLAY; LOAD DEFAULTS asks, and NO
    # closes the question; BACK returns the title.
    'settings-page': (None, [
        'waitMsec 9500', 'openq4_retainedGui focus nav_settings', 'openq4_retainedGui report', *menu_step('accept'), 'waitMsec 1200',
        'ui_retainedStatus',
        'openq4_retainedGui report', 'screenshot "screenshots/settings-page.tga"',
        *menu_step('accept'), 'waitMsec 800', 'openq4_retainedGui event onBack', 'waitMsec 800', 'openq4_retainedGui report',
        'screenshot "screenshots/settings-page-controls-back.tga"',
        'openq4_retainedGui focus display', *menu_step('accept'), 'waitMsec 1500', 'openq4_guiGet p_settings_sys::visible',
        'ui_retainedStatus', 'screenshot "screenshots/settings-page-display.tga"',
        'openq4_guiAction set_b_back', 'waitMsec 1500', 'openq4_retainedGui report', 'ui_retainedStatus',
        'screenshot "screenshots/settings-page-display-back.tga"',
        'openq4_retainedGui focus defaults', *menu_step('accept'), 'waitMsec 600', 'screenshot "screenshots/settings-page-defaults.tga"',
        'openq4_retainedGui focus defaultsModal_no', *menu_step('accept'), 'waitMsec 600', 'openq4_retainedGui report',
        'openq4_retainedGui event onBack', 'waitMsec 1500', 'ui_retainedStatus', 'openq4_retainedGui report',
        'screenshot "screenshots/settings-page-back.tga"',
    ]),
    # The settings sub-pages (sections 9 and 13.7): GAME OPTIONS on the SETTINGS
    # page goes one level deeper, captured mid-change and presented; each of
    # its five tabs, a list unfolded on the last; Back climbs out onto GAME
    # OPTIONS mid-change and at rest; CONTROLS and its next tab; AUDIO and its
    # Back.
    'settings-subpages': (None, [
        'waitMsec 9500', 'openq4_retainedGui focus nav_settings', *menu_step('accept'), 'waitMsec 1200',
        'openq4_retainedGui focus game', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 175', 'screenshot "screenshots/subpages-game-change.tga"', 'waitMsec 1200', 'ui_retainedStatus',
        'openq4_retainedGui report', 'screenshot "screenshots/subpages-game.tga"',
        *[step for name in ('mouse', 'controller', 'hud', 'vr') for step in (
            'openq4_retainedGui event onTabNext', 'waitMsec 400', 'openq4_retainedGui report',
            f'screenshot "screenshots/subpages-game-{name}.tga"')],
        'openq4_retainedGui focus game_vrsnap', 'waitMsec 100', 'openq4_retainedGui choice open game_vrsnap', 'waitMsec 400',
        'screenshot "screenshots/subpages-game-list.tga"', *menu_step('back'),
        # A typed sensitivity with a third decimal place shows the field's own
        # range over the row's label; the controller tab's last row scrolls
        # into a tall view's single column.
        'openq4_retainedGui event tab_mouse', 'waitMsec 400', 'openq4_retainedGui focus game_msensitivity_number',
        'openq4_retainedGui number begin game_msensitivity_number', 'openq4_retainedGui number replace game_msensitivity_number "7.255"',
        'waitMsec 300', 'openq4_retainedGui report', 'screenshot "screenshots/subpages-game-entry.tga"',
        'openq4_retainedGui number cancel game_msensitivity_number', 'openq4_retainedGui event tab_controller', 'waitMsec 400',
        'openq4_retainedGui focus game_swapconfirm', 'waitMsec 300', 'screenshot "screenshots/subpages-game-last.tga"',
        'openq4_retainedGui event onBack', 'waitMsec 150', 'screenshot "screenshots/subpages-game-back.tga"', 'waitMsec 1200',
        'openq4_retainedGui report', 'screenshot "screenshots/subpages-returned.tga"',
        'openq4_retainedGui focus controls', *menu_step('accept'), 'waitMsec 1500', 'openq4_retainedGui report',
        'screenshot "screenshots/subpages-controls.tga"',
        'openq4_retainedGui event onTabNext', 'waitMsec 400', 'screenshot "screenshots/subpages-controls-weapons.tga"',
        'openq4_retainedGui event onBack', 'waitMsec 1500',
        'openq4_retainedGui focus audio', *menu_step('accept'), 'waitMsec 1500', 'openq4_retainedGui report',
        'screenshot "screenshots/subpages-audio.tga"',
        'openq4_retainedGui event onBack', 'waitMsec 1500', 'openq4_retainedGui report', 'ui_retainedStatus',
        'screenshot "screenshots/subpages-audio-back.tga"',
    ]),
    # Opted into, DISPLAY on the SETTINGS page goes one level deeper to the
    # SYSTEM page, captured mid-change and presented; its Back climbs out onto
    # DISPLAY mid-change and at rest.
    'settings-system': (None, [
        'waitMsec 14000', 'openq4_retainedGui focus nav_settings', *menu_step('accept'), 'waitMsec 1200',
        'openq4_retainedGui focus display', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 175', 'screenshot "screenshots/settings-system-change.tga"', 'waitMsec 1200', 'ui_retainedStatus',
        'openq4_system report', 'openq4_retainedGui report', 'screenshot "screenshots/settings-system.tga"',
        'openq4_retainedGui event onTabNext', 'waitMsec 400', 'screenshot "screenshots/settings-system-screen.tga"',
        'openq4_retainedGui event onTabNext', 'waitMsec 400', 'screenshot "screenshots/settings-system-image.tga"',
        'openq4_retainedGui event onBack', 'waitMsec 150', 'screenshot "screenshots/settings-system-back.tga"', 'waitMsec 1200',
        'openq4_system report', 'openq4_retainedGui report', 'ui_retainedStatus',
        'screenshot "screenshots/settings-system-returned.tga"',
    ]),
    # The menu cursor (section 6), placed without a device (openq4_retainedGui
    # pointer, in the 640x480 screen): the arrow over the title's emblem, the
    # arrow again over SINGLE PLAYER, a control a press would reach, and the
    # I-beam over CREATE SERVER's server name field.
    'cursor': (None, [
        'waitMsec 9500', 'openq4_retainedGui pointer 470 150', 'waitMsec 300', 'screenshot "screenshots/cursor-arrow.tga"',
        'openq4_retainedGui pointer 120 214', 'waitMsec 300', 'openq4_retainedGui report', 'screenshot "screenshots/cursor-control.tga"',
        'openq4_retainedGui focus nav_multiplayer', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 1200',
        'openq4_retainedGui focus create', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 1400',
        'openq4_retainedGui inspect "create-name"', 'openq4_retainedGui pointer 400 85', 'waitMsec 300', 'openq4_retainedGui report',
        'screenshot "screenshots/cursor-text.tga"',
    ]),
    # LOAD DEFAULTS' YES restores the defaults: a changed sensitivity returns
    # to its default.
    'settings-defaults': (None, [
        'waitMsec 9500', 'sensitivity 9', 'openq4_retainedGui focus nav_settings', *menu_step('accept'), 'waitMsec 1200',
        'openq4_retainedGui focus defaults', *menu_step('accept'), 'waitMsec 600', 'openq4_retainedGui focus defaultsModal_yes',
        # cvar_restart resets the log CVars too, so the run logs on in the
        # continuation file.
        *menu_step('accept'), 'waitMsec 2000', 'set logFile 2', 'set logFileName "logs/openq4-match.log"', 'sensitivity',
        'openq4_retainedGui report', 'screenshot "screenshots/settings-defaults.tga"',
    ]),
    # SETTINGS on the pause opens the page over the level; BACK returns the pause.
    'settings-pause': ('game/airdefense1', [
        'openq4_assertMenuActivation 10000', 'waitMsec 1200', 'openq4_retainedGui focus nav_settings', *menu_step('accept'),
        'waitMsec 1200', 'ui_retainedStatus', 'openq4_retainedGui report', 'screenshot "screenshots/settings-pause.tga"',
        'openq4_retainedGui event onBack', 'waitMsec 1500', 'ui_retainedStatus', 'openq4_retainedGui report',
        'screenshot "screenshots/settings-pause-back.tga"',
    ]),
    'settings-strogg': ('game/recomp', [
        'openq4_assertMenuActivation 10000', 'waitMsec 1200', 'openq4_retainedGui focus nav_settings', *menu_step('accept'),
        'waitMsec 1200', 'ui_retainedStatus', 'openq4_retainedGui report', 'screenshot "screenshots/settings-strogg.tga"',
        'openq4_retainedGui event onBack', 'waitMsec 1500', 'ui_retainedStatus', 'openq4_retainedGui report',
        'screenshot "screenshots/settings-strogg-back.tga"',
    ]),
    'fallback-settings': (None, [
        'waitMsec 9500', 'openq4_retainedGui focus nav_settings', *menu_step('accept'), 'waitMsec 1500',
        'ui_retainedStatus', 'openq4_guiGet p_settings::visible', 'screenshot "screenshots/fallback-settings.tga"',
    ]),
    # The settings screen with the retained pages: it arrives on no panel,
    # titled SETTINGS, every button showing; CONTROLS opens its page, and Back
    # returns to the screen as it was; the stock DISPLAY panel opens, and
    # CONTROLS still opens its page over it.
    'settings-screen': (None, [
        'waitMsec 4000', 'openq4_guiAction main_b_settings', 'waitMsec 1500', 'openq4_guiGet p_settings_ctrls::visible',
        'openq4_guiGet set_b_controls::visible', 'screenshot "screenshots/settings-screen.tga"',
        'openq4_guiAction set_b_controls', 'waitMsec 1500', 'openq4_retainedGui report', 'openq4_retainedGui event onBack',
        'waitMsec 1000', 'openq4_guiGet p_settings_ctrls::visible', 'screenshot "screenshots/settings-screen-back.tga"',
        'openq4_guiAction set_b_system', 'waitMsec 1500', 'openq4_guiGet p_settings_sys::visible',
        'screenshot "screenshots/settings-screen-display.tga"', 'openq4_guiAction set_b_controls', 'waitMsec 1500',
        'openq4_retainedGui report', 'openq4_retainedGui event onBack', 'waitMsec 1000', 'openq4_guiGet p_settings_sys::visible',
        'screenshot "screenshots/settings-screen-display-back.tga"',
    ]),
    # The multiplayer menu's CONTROLS route, as its GAME OPTIONS route.
    'controls-mp-route': (None, [
        'waitMsec 4000', 'GuiEvent fromMp_toControls', 'waitMsec 1500', 'openq4_retainedGui report', 'ui_retainedStatus',
        'screenshot "screenshots/controls-mp-route.tga"', 'openq4_retainedGui event onBack', 'waitMsec 1000', 'openq4_retainedGui report',
        'openq4_guiGet desktop::dest', 'openq4_guiGet desktop::active', 'ui_retainedStatus',
    ]),
    # A CONTROLS page that cannot load: the same press opens the stock page.
    'fallback-controls': (None, [
        'waitMsec 4000', 'openq4_guiAction main_b_settings', 'waitMsec 1500', 'openq4_guiAction set_b_controls',
        'waitMsec 1500', 'openq4_guiGet p_settings_ctrls::visible', 'ui_retainedStatus', 'screenshot "screenshots/fallback-controls.tga"',
    ]),
    # A GAME OPTIONS page that cannot load: the multiplayer route opens the stock page.
    'fallback-game-mp': (None, [
        'waitMsec 4000', 'GuiEvent fromMp_toGameoptions', 'waitMsec 2500', 'openq4_guiGet p_settings_game::visible',
        'openq4_guiGet desktop::active', 'ui_retainedStatus', 'screenshot "screenshots/fallback-game-mp.tga"',
    ]),
    # A GAME OPTIONS page that cannot load: the same press opens the stock page.
    'fallback-game': (None, [
        'waitMsec 4000', 'openq4_guiAction main_b_settings', 'waitMsec 1500', 'openq4_guiAction set_b_gameoptions',
        'waitMsec 1500', 'openq4_guiGet p_settings_game::visible', 'ui_retainedStatus', 'screenshot "screenshots/fallback-game.tga"',
    ]),
    # An AUDIO page that cannot load: the same press opens the stock page.
    'fallback-audio': (None, [
        'waitMsec 4000', 'openq4_guiAction main_b_settings', 'waitMsec 1500', 'openq4_guiAction set_b_audio',
        'waitMsec 1500', 'openq4_guiGet p_settings_audio::visible', 'ui_retainedStatus', 'screenshot "screenshots/fallback-audio.tga"',
    ]),
    # Join Game's notices (section 13.13): taking the seeded loopback server
    # off the favorites says so with Undo; putting it back and taking it off
    # again stack three notices, newest at the top; Undo on the newest puts
    # the server back and that notice leaves; the rest leave after their
    # holds (4 s, 8 s with Undo).
    'join-notices': (None, [
        'waitMsec 12000',
        'openq4_retainedGui focus nav_multiplayer', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2500', 'openq4_retainedGui focus join', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2500', 'openq4_retainedGui focus join_internet', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 4800', 'openq4_retainedGui focus "server-0"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 400', 'openq4_retainedGui focus join_favorite', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 400', 'openq4_retainedGui inspect "server-0-fav"', 'openq4_retainedGui inspect "notice-0"', 'openq4_retainedGui inspect "notice-1"', 'openq4_retainedGui inspect "notice-2"', 'screenshot "screenshots/join-notice-removed.tga"',
        'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 400',
        'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 400',
        'openq4_retainedGui inspect "server-0-fav"', 'openq4_retainedGui inspect "notice-0"', 'openq4_retainedGui inspect "notice-1"', 'openq4_retainedGui inspect "notice-2"', 'screenshot "screenshots/join-notices-stacked.tga"',
        'openq4_retainedGui focus "notice-0-action"', 'waitMsec 300', 'screenshot "screenshots/join-notice-undo-focus.tga"',
        'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 400',
        'openq4_retainedGui inspect "server-0-fav"', 'openq4_retainedGui inspect "notice-0"', 'openq4_retainedGui inspect "notice-1"', 'openq4_retainedGui inspect "notice-2"', 'screenshot "screenshots/join-notice-undone.tga"',
        'waitMsec 9000', 'openq4_retainedGui inspect "notice-0"', 'openq4_retainedGui inspect "notice-1"', 'openq4_retainedGui inspect "notice-2"', 'screenshot "screenshots/join-notices-gone.tga"',
    ]),
    # The Join Game list's empty state: NOT EMPTY and Tourney together hide the
    # seeded loopback server (SCENARIO_SERVERS), so the list says none matches
    # under NO MATCHING SERVERS and offers CLEAR FILTERS, which clears both
    # filters at once and brings the server back.
    'join-empty-clear': (None, [
        'waitMsec 12000',
        'openq4_retainedGui focus nav_multiplayer', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2500', 'openq4_retainedGui focus join', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2500', 'openq4_retainedGui focus join_internet', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 4800', 'openq4_retainedGui inspect "server-0"', 'screenshot "screenshots/join-empty-list.tga"',
        'openq4_retainedGui focus join_filter_empty', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 300',
        'openq4_retainedGui focus join_gametype_next', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 300',
        'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 600',
        'gui_filter_players', 'gui_filter_gameType', 'openq4_retainedGui inspect "server-0"',
        'openq4_retainedGui inspect "join-empty-heading"', 'openq4_retainedGui inspect "join_emptyclear"',
        'screenshot "screenshots/join-empty-filtered.tga"',
        'openq4_retainedGui focus join_emptyclear', 'waitMsec 300', 'screenshot "screenshots/join-empty-clear-focus.tga"',
        'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 600',
        'gui_filter_players', 'gui_filter_gameType', 'openq4_retainedGui inspect "server-0"', 'openq4_retainedGui inspect "join-empty-heading"',
        'screenshot "screenshots/join-empty-cleared.tga"',
    ]),
    # JOIN IP ADDRESS on the Join Game sub-page: an address it cannot join
    # keeps the dialog with the reason (spaces fail before any lookup); the
    # loopback dedicated server's address (SCENARIO_SERVERS), entered, joins
    # it and closes the dialog, the game module reloading into game_mp.
    'joingameaddress': (None, [
        'waitMsec 7000',
        'openq4_retainedGui focus nav_multiplayer', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2500', 'openq4_retainedGui focus join', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2500', 'openq4_retainedGui focus join_ip', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2500', 'openq4_retainedGui report', 'openq4_retainedGui inspect "join-yourip-value"',
        'screenshot "screenshots/joingameaddress-open.tga"',
        'openq4_retainedGui number begin "join-address"', 'openq4_retainedGui number input "join-address" "not an address"',
        'openq4_retainedGui number commit "join-address"', 'waitMsec 1000', 'openq4_retainedGui inspect "join-address-error"',
        'openq4_retainedGui report', 'screenshot "screenshots/joingameaddress-error.tga"',
        'openq4_retainedGui number begin "join-address"', 'openq4_retainedGui number select "join-address" 0 14',
        'openq4_retainedGui number replace "join-address" "127.0.0.1:27962"',
        'set logFileName "logs/openq4-match.log"',
        'openq4_retainedGui number commit "join-address"', AFTER_MAP,
        'si_map', 'ui_retainedStatus', 'screenshot "screenshots/joingameaddress-match.tga"', 'disconnect', 'waitMsec 2000',
    ]),
    # The classic server browser (ui_retained 0) shows the scan's progress
    # again: retail's scan published its notice and fraction (gui::loadNotice,
    # gui::loadPct) for the stock load bar, which openQ4's had dropped. GET
    # NEW LIST asks the master server, which the loopback cannot reach, and
    # the seeded favorite, a loopback dedicated server (SCENARIO_SERVERS),
    # answers: the bar fills and names the count.
    'stockbrowser': (None, [
        'waitMsec 7000', 'openq4_browser open', 'waitMsec 2500', 'openq4_browser source internet', 'waitMsec 2500',
        'openq4_browser action updateServers', 'waitMsec 300', 'openq4_browser report',
        'screenshot "screenshots/stockbrowser-scanning.tga"', 'waitMsec 4500', 'openq4_browser report',
        'screenshot "screenshots/stockbrowser-scanned.tga"',
        # The server tooltip again (retail's, which openQ4's scan had kept
        # hidden): the listed server's row treated as hovered, without a
        # pointer (openq4_browser hover), then no row, which hides it.
        'openq4_browser hover 0', 'waitMsec 1000', 'openq4_browser report', 'screenshot "screenshots/stockbrowser-tooltip.tga"',
        'openq4_browser hover -1', 'waitMsec 1000', 'openq4_browser report',
    ]),
    # The Credits page: CREDITS hands off and the page presents at 550 ms on
    # its opaque backing; the Raven logo lights and goes out, then the first
    # screen's lines arrive in runes and translate to Latin under the scan
    # bars, and the screen leaves for the next. Back returns the title.
    'credits': (None, [
        'waitMsec 4000', 'openq4_retainedGui focus link_credits', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 1300', 'ui_retainedStatus', 'screenshot "screenshots/credits-logo.tga"',
        'waitMsec 4000', 'screenshot "screenshots/credits-runes.tga"', 'waitMsec 2500', 'openq4_retainedGui report',
        'screenshot "screenshots/credits-translated.tga"', 'waitMsec 3300', 'screenshot "screenshots/credits-next.tga"',
        'waitMsec 3000', 'screenshot "screenshots/credits-second.tga"',
        'openq4_retainedGui focus credits_back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 900', 'screenshot "screenshots/credits-back.tga"',
    ]),
    # The restart screen (FLOW-005): the player dies in the level, and when the
    # respawn wait runs out the level unloads and the screen presents with
    # RESTART focused. Each plate explains itself in the line under the
    # dialog; EXIT GAME asks and NO closes the question; RESTART reloads the
    # level from its autosave.
    'death': ('game/airdefense1', [
        'waitMsec 1500', 'kill', 'waitMsec 15000', 'ui_retainedStatus', 'openq4_retainedGui report',
        'screenshot "screenshots/death.tga"',
        'openq4_retainedGui focus death_load', 'waitMsec 300', 'screenshot "screenshots/death-load.tga"',
        'openq4_retainedGui focus death_exit', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 400', 'screenshot "screenshots/death-exit.tga"',
        'openq4_retainedGui focus exitModal_no', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        # Back keeps the screen and returns the focus to RESTART.
        'waitMsec 500', 'openq4_retainedGui focus death_exit', 'openq4_retainedGui menu back 1', 'openq4_retainedGui menu back 0',
        'waitMsec 1500', 'ui_retainedStatus', 'openq4_retainedGui report', 'screenshot "screenshots/death-back.tga"',
        'openq4_retainedGui focus death_restart', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 12000', 'ui_retainedStatus', 'screenshot "screenshots/death-restarted.tga"',
    ]),
    # The death screen's LOAD GAME: the retained Load Game page over the title.
    'death-load': ('game/airdefense1', [
        'waitMsec 1000', 'saveGame "Before death"', 'waitMsec 1500', 'kill', 'waitMsec 15000', 'ui_retainedStatus',
        'openq4_retainedGui focus death_load', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 3000', 'ui_retainedStatus', 'openq4_retainedGui report', 'screenshot "screenshots/death-load-page.tga"',
    ]),
    # Back on the Single Player page the title's SINGLE PLAYER opened returns
    # the title, its bands coming home and SINGLE PLAYER still focused.
    # Each section summarizes itself in the content column while it is the
    # subject (section 13.7): Single Player's CAMPAIGN (no save in a fresh
    # profile, so no picture) and ARENA over its chosen match's levelshot;
    # Campaign's NEW GAME and CHAPTERS over the active campaign's first and
    # furthest levels (the same in a fresh profile); Multiplayer's JOIN GAME
    # and CREATE SERVER over si_map's; and SETTINGS' sections, text alone.
    # BACK and LOAD DEFAULTS keep the page's description.
    'summaries': (None, [
        # A fresh build's first start can run past the usual wait; the title
        # refuses focus until it has arrived.
        'waitMsec 7000', 'openq4_retainedGui focus nav_singleplayer', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 1800', 'openq4_retainedGui inspect "summary-campaign"',
        'screenshot "screenshots/summaries-campaign.tga"',
        'openq4_retainedGui focus arena', 'waitMsec 300', 'openq4_retainedGui inspect "summary-arena-shot"',
        'screenshot "screenshots/summaries-arena.tga"',
        'openq4_retainedGui focus campaign', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1200', 'openq4_retainedGui inspect "summary-newgame-shot"', 'screenshot "screenshots/summaries-newgame.tga"',
        'openq4_retainedGui focus chapters', 'waitMsec 300', 'screenshot "screenshots/summaries-chapters.tga"',
        'openq4_retainedGui focus quake4', 'waitMsec 300', 'screenshot "screenshots/summaries-quake4.tga"',
        'openq4_retainedGui menu back 1', 'openq4_retainedGui menu back 0', 'waitMsec 1200',
        'openq4_retainedGui focus back', 'waitMsec 300', 'openq4_retainedGui inspect "description"',
        'screenshot "screenshots/summaries-back.tga"',
        'openq4_retainedGui menu back 1', 'openq4_retainedGui menu back 0', 'waitMsec 1500',
        'openq4_retainedGui focus nav_multiplayer', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2500', 'openq4_retainedGui inspect "summary-join"', 'screenshot "screenshots/summaries-join.tga"',
        'openq4_retainedGui focus create', 'waitMsec 300', 'openq4_retainedGui inspect "summary-create-shot"',
        'screenshot "screenshots/summaries-create.tga"',
        'openq4_retainedGui focus setup', 'waitMsec 300', 'screenshot "screenshots/summaries-setup.tga"',
        'openq4_retainedGui focus back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 1500',
        'openq4_retainedGui focus nav_settings', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2500', 'openq4_retainedGui inspect "summary-controls"', 'screenshot "screenshots/summaries-controls.tga"',
        'openq4_retainedGui focus display', 'waitMsec 300', 'screenshot "screenshots/summaries-display.tga"',
        'openq4_retainedGui focus defaults', 'waitMsec 300', 'screenshot "screenshots/summaries-defaults.tga"',
    ]),
    'singleplayer-back': (None, [
        'waitMsec 4000', 'openq4_retainedGui focus nav_singleplayer', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 1800', 'ui_retainedStatus', 'screenshot "screenshots/singleplayer-page.tga"',
        'openq4_retainedGui menu back 1', 'openq4_retainedGui menu back 0', 'waitMsec 1500', 'ui_retainedStatus',
        'openq4_retainedGui report', 'screenshot "screenshots/singleplayer-back.tga"',
    ]),
    # UPDATES checks in place: the title keeps its content, and the answer, a
    # stock message box, comes over the retained title.
    'updates': (None, [
        'waitMsec 4000', 'openq4_retainedGui focus link_updates', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 400', 'ui_retainedStatus', 'screenshot "screenshots/updates-checking.tga"',
        'waitMsec 3500', 'ui_retainedStatus', 'screenshot "screenshots/updates-answer.tga"',
    ]),
    # Left and right step Create Server's Gametype with its list closed, and
    # wrap at either end.
    'createserver-stepping': (None, [
        'waitMsec 4000',
        'openq4_retainedGui focus nav_multiplayer', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1500', 'openq4_retainedGui focus create', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 2000', 'openq4_retainedGui focus "create-gametype"', 'waitMsec 200', 'si_gameType',
        'openq4_retainedGui menu right 1', 'openq4_retainedGui menu right 0', 'waitMsec 400', 'si_gameType',
        'openq4_retainedGui widget "create-gametype"', 'screenshot "screenshots/stepping-right.tga"',
        'openq4_retainedGui menu left 1', 'openq4_retainedGui menu left 0', 'waitMsec 200',
        'openq4_retainedGui menu left 1', 'openq4_retainedGui menu left 0', 'waitMsec 400', 'si_gameType',
        'openq4_retainedGui widget "create-gametype"', 'screenshot "screenshots/stepping-left.tga"',
    ]),
    # MODS on the title (section 6): the list dialog opens over the title with
    # the installed mods, the current game's selected and focused, and the
    # wordmark dimmed; LOAD MOD takes the focus, and CANCEL closes the dialog.
    'mods': (None, [
        'waitMsec 4000', 'openq4_retainedGui focus link_mods', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 100', 'screenshot "screenshots/mods-opening.tga"', 'waitMsec 500',
        'openq4_retainedGui report', 'screenshot "screenshots/mods.tga"',
        'openq4_retainedGui focus modsModal_yes', 'waitMsec 300', 'screenshot "screenshots/mods-load.tga"',
        'openq4_retainedGui focus modsModal_no', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 600', 'openq4_retainedGui report', 'screenshot "screenshots/mods-closed.tga"',
    ]),
    # The Demos sub-page (FLOW-013, FLOW-050): two render demos recorded in
    # the level, then the title's MULTIPLAYER and the page's DEMOS go deeper
    # and the sub-page presents at 350 ms with the newest selected. A filter
    # that hides both shows the filtered empty state and clears again; DELETE
    # asks naming the demo, and YES deletes it and selects the older one;
    # PLAY plays it (8 s long, shot 2.5 s in), and demoStop ends playback.
    # The title then reopens on the Multiplayer page's DEMOS through Back.
    'demos': ('game/airdefense1', [
        'waitMsec 1000', 'recordDemo retainedolder', 'waitMsec 8000', 'stopRecording', 'waitMsec 500',
        'recordDemo retainednewer', 'waitMsec 2500', 'stopRecording', 'waitMsec 500', 'disconnect', 'waitMsec 4000',
        'openq4_retainedGui focus nav_multiplayer', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1500', 'openq4_retainedGui focus demos', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 175', 'screenshot "screenshots/demos-handoff.tga"', 'waitMsec 1200', 'ui_retainedStatus',
        'openq4_retainedGui report', 'screenshot "screenshots/demos.tga"',
        'openq4_retainedGui focus "tag-mvd"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 300', 'openq4_retainedGui inspect "demo_emptyclear"', 'screenshot "screenshots/demos-filtered.tga"',
        'openq4_retainedGui focus demo_emptyclear', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 300', 'openq4_retainedGui inspect "demo_emptyclear"', 'screenshot "screenshots/demos-cleared.tga"',
        'openq4_retainedGui focus demo_delete', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 400', 'screenshot "screenshots/demos-delete.tga"',
        'openq4_retainedGui focus deleteModal_yes', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 600', 'openq4_retainedGui report', 'screenshot "screenshots/demos-deleted.tga"',
        'openq4_retainedGui focus demo_play', 'waitMsec 300', 'screenshot "screenshots/demos-play.tga"',
        'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 2500', 'ui_retainedStatus',
        'screenshot "screenshots/demos-playing.tga"', 'demoStop', 'waitMsec 3000', 'ui_retainedStatus',
        'screenshot "screenshots/demos-stopped.tga"',
    ]),
    # Back from the Demos sub-page climbs out onto the Multiplayer page's
    # DEMOS: the sub-page sweeps away over 300 ms and the page fades back in.
    'demosback': (None, [
        'waitMsec 7000',
        'openq4_retainedGui focus nav_multiplayer', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1500', 'openq4_retainedGui focus demos', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1500', 'ui_retainedStatus', 'openq4_retainedGui report', 'screenshot "screenshots/demosback-empty.tga"',
        'openq4_retainedGui focus demo_back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 150', 'screenshot "screenshots/demosback-leaving.tga"', 'waitMsec 1200', 'ui_retainedStatus',
        'openq4_retainedGui report', 'screenshot "screenshots/demosback-returned.tga"',
    ]),
    # The Restart Level page (FLOW-001): the pause's RESTART LEVEL hands off to
    # it at the level's difficulty. A plate chooses another, which the page
    # lights and describes; Back leaves the difficulty as it was, so the page
    # reopens at the level's; RESTART LEVEL restarts the level at the choice.
    'restartlevel': ('game/airdefense1', [
        'waitMsec 1000', 'openq4_assertMenuActivation 10000', 'waitMsec 1200', 'ui_retainedStatus',
        'openq4_retainedGui focus nav_restart', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 250', 'screenshot "screenshots/restartlevel-handoff.tga"', 'waitMsec 800', 'ui_retainedStatus',
        'openq4_retainedGui report', 'screenshot "screenshots/restartlevel.tga"',
        'openq4_retainedGui focus "skill-3"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 300',
        'screenshot "screenshots/restartlevel-chosen.tga"',
        'openq4_retainedGui focus restart_back', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 900', 'screenshot "screenshots/restartlevel-home.tga"',
        'openq4_retainedGui focus nav_restart', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0',
        'waitMsec 1100', 'screenshot "screenshots/restartlevel-reopened.tga"',
        'openq4_retainedGui focus "skill-3"', 'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 300',
        'openq4_retainedGui focus restart_go', 'waitMsec 300', 'screenshot "screenshots/restartlevel-go.tga"',
        'openq4_retainedGui menu accept 1', 'openq4_retainedGui menu accept 0', 'waitMsec 12000', 'ui_retainedStatus',
        'screenshot "screenshots/restartlevel-restarted.tga"',
    ]),
    # A mod with its own copies of the stock loading screens: the level loads
    # under the mod's screen, and the retained loading screen never loads.
    'fallback-loading': ('game/airdefense2', [
        'ui_retainedStatus', 'screenshot "screenshots/fallback-loading.tga"',
    ]),
    # A Single Player selector that cannot load: the stock selector opens.
    'fallback-campaign': (None, [
        'waitMsec 4000', 'openq4_guiAction main_b_newgame', 'waitMsec 2500', 'openq4_system report', 'ui_retainedStatus',
        'screenshot "screenshots/fallback-campaign.tga"',
    ]),
}

# Not a retained document: it fails to load with diagnostics.
BROKEN_DOCUMENT = '{ "openq4": "not a retained document" }\n'

# A mod's own loading screen under a stock name: a plain blue card.
MOD_LOADING_GUI = '''windowDef Desktop
{
	rect	0, 0, 640, 480
	backcolor	0.05, 0.1, 0.3, 1
	windowDef modLoading
	{
		rect	0, 200, 640, 40
		text	"MOD LOADING SCREEN"
		textscale	0.5
		textalign	1
		forecolor	1, 1, 1, 1
	}
}
'''


def stock_menu_without_demos(runtime: Path) -> str:
    """The installed main menu with its Demos page renamed, as a mod's menu might lack it."""
    with zipfile.ZipFile(runtime / 'baseoq4' / 'pak0.pk4') as pack:
        menu = pack.read('guis/mainmenu.gui').decode('utf-8', errors='surrogateescape')
    assert 'windowDef main_b_demos' in menu
    return menu.replace('main_b_demos', 'main_b_records')


# CVars a scenario sets beyond the common ones.
# Ports a scenario holds on the loopback interface while it runs, and the
# cfg files it chains to (the last one ends the run).
SCENARIO_PORTS = {'arenafail': 27961}
# A dedicated server a scenario starts first (its loopback port, map and
# name), from the runtime with its own savepath; it quits once the client is
# done.
SCENARIO_SERVERS = {'mpadminremote': {'port': 27964, 'map': 'mp/q4dm1', 'name': 'Retained Admin Arena',
                                      'cvars': {'net_serverRemoteConsolePassword': 'openq4-admin-probe', 'bot_minPlayers': '2',
                                               'net_allowCheats': '1'}},
                    'joingame': {'port': 27962, 'map': 'mp/q4dm1', 'name': 'Retained Join Arena'},
                    'join-details-view': {'port': 27962, 'map': 'mp/q4dm1', 'name': 'Retained Join Arena'},
                    'join-view-glyphs': {'port': 27962, 'map': 'mp/q4dm1', 'name': 'Retained Join Arena'},
                    'joingameaddress': {'port': 27962, 'map': 'mp/q4dm1', 'name': 'Retained Join Arena'},
                    'joinfilters': {'port': 27962, 'map': 'mp/q4dm1', 'name': 'Retained Join Arena'},
                    'join-empty-clear': {'port': 27962, 'map': 'mp/q4dm1', 'name': 'Retained Join Arena'},
                    'join-notices': {'port': 27962, 'map': 'mp/q4dm1', 'name': 'Retained Join Arena'},
                    'stockbrowser': {'port': 27962, 'map': 'mp/q4dm1', 'name': 'Retained Join Arena'}}
SCENARIO_FILES = {'arenafail': {
    'retained_arenafail_defer.cfg': ['vstr arenaAfter'],
    'retained_arenafail_after.cfg': [
        # In the hidden capture window the reloaded module presents its first
        # frame at a screenshot, which reads black; the ledger reveals from it.
        'waitMsec 3000', 'screenshot "screenshots/arenafail-reloaded.tga"', 'waitMsec 3000',
        'ui_arenaCampaignActive', 'ui_arenaActiveMatch', 'ui_retainedStatus', 'openq4_retainedGui report',
        'screenshot "screenshots/arenafail-ledger.tga"', 'openq4_retainedGui focus resultModal_no', 'openq4_retainedGui menu accept 1',
        'openq4_retainedGui menu accept 0', 'waitMsec 900', 'ui_arenaProgress', 'openq4_retainedGui report',
        'screenshot "screenshots/arenafail-browser.tga"'],
}}
SCENARIO_CVARS = {'mpcard-prompts': {'ui_retainedMultiplayer': '1'}, 'mpcard-triggers': {'ui_retainedMultiplayer': '1'},
                  'settings-controller': {'ui_retained': '0'},
                  'mpadmin': {'bot_minPlayers': '3'}, 'mpadminremote': {'net_ip': '127.0.0.1', 'ui_autoJoin': '1', 'ui_spectate': 'Play'},
                  # An Arena match is a local multiplayer game, where the menu probe is a cheat.
                  'arenapause': {'net_allowCheats': '1'}, 'system-optin': {'ui_retainedSystem': '1'}, 'fallback-system': {'ui_retainedSystem': '1'},
                  'system-mp-route': {'ui_retainedSystem': '1'}, 'fallback-system-mp': {'ui_retainedSystem': '1'},
                  'system-mp-game': {'ui_retainedSystem': '1'}, 'system-menu-apply': {'ui_retainedSystem': '1'},
                  'settings-system': {'ui_retainedSystem': '1'},
                  'fallback-loading': {'developer': '1'}, 'subpage-reduced': {'ui_retainedReducedMotion': '1'},
                  'arenafail': {'net_port': str(SCENARIO_PORTS['arenafail']), 'net_ip': '127.0.0.1'},
                  'multiplayer': {'net_allowCheats': '1'}, 'summaries': {'si_map': 'mp/q4dm5'},
                  'createserverstart': {'net_ip': '127.0.0.1'},
                  'joingame': {'net_ip': '127.0.0.1'}, 'joingameaddress': {'net_ip': '127.0.0.1'},
                  'join-details-view': {'net_ip': '127.0.0.1'}, 'join-view-glyphs': {'net_ip': '127.0.0.1'},
                  'joinfilters': {'net_ip': '127.0.0.1', 'gui_filter_players': '0', 'gui_filter_password': '0', 'gui_filter_gameType': '0'},
                  'join-empty-clear': {'net_ip': '127.0.0.1', 'gui_filter_players': '0', 'gui_filter_password': '0', 'gui_filter_gameType': '0'},
                  'join-notices': {'net_ip': '127.0.0.1', 'gui_filter_players': '0', 'gui_filter_password': '0', 'gui_filter_gameType': '0'},
                  'stockbrowser': {'net_ip': '127.0.0.1', 'ui_retained': '0'}}

# The private mod the fallback scenarios run as, and the files it provides.
FALLBACK_MOD = 'retainedfallback'
OVERRIDES = {
    'fallback-document': {'guis/menu/title.q4ui': lambda runtime: BROKEN_DOCUMENT},
    'fallback-menu': {'guis/mainmenu.gui': stock_menu_without_demos},
    'fallback-system': {'guis/menu/settings/system.q4ui': lambda runtime: BROKEN_DOCUMENT},
    'fallback-system-mp': {'guis/menu/settings/system.q4ui': lambda runtime: BROKEN_DOCUMENT},
    'fallback-audio': {'guis/menu/settings/audio.q4ui': lambda runtime: BROKEN_DOCUMENT},
    'fallback-game': {'guis/menu/settings/game.q4ui': lambda runtime: BROKEN_DOCUMENT},
    'fallback-game-mp': {'guis/menu/settings/game.q4ui': lambda runtime: BROKEN_DOCUMENT},
    'fallback-controls': {'guis/menu/settings/controls.q4ui': lambda runtime: BROKEN_DOCUMENT},
    'fallback-settings': {'guis/menu/settings.q4ui': lambda runtime: BROKEN_DOCUMENT},
    'fallback-campaign': {'guis/menu/singleplayer.q4ui': lambda runtime: BROKEN_DOCUMENT},
    'fallback-loading': {'guis/loading/splevel.gui': lambda runtime: MOD_LOADING_GUI,
                         'guis/loading/generic.gui': lambda runtime: MOD_LOADING_GUI},
}


def damaged_save(runtime: Path) -> bytes:
    """A save whose header the Load Game page lists (Quake4, version 1834,
    game/airdefense1, no entity filter, 32 empty player dictionaries) but
    whose payload LoadGame refuses: its marker is neither openQ4's nor the
    build's."""
    def string(text: str) -> bytes:
        data = text.encode('ascii')
        return struct.pack('<i', len(data)) + data
    header = string('Quake4') + struct.pack('<i', 1834) + string('game/airdefense1') + string('') + struct.pack('<i', 0) * 32
    return header + struct.pack('<i', 0x2BAD) + bytes(64)


# The damaged save's menu label, as SaveGame writes one.
DAMAGED_DESCRIPTION = '"damaged"\r\n"^:Damaged save\t^0Air Defense Trenches"\r\n""\r\n'

# An autosave's menu label: it names its level's loadscreen, so the Save Game
# page refuses to save over it.
AUTOSAVE_DESCRIPTION = '"auto"\r\n"^:Autosave\t^0AIR DEFENSE BUNKER"\r\n"gfx/guis/loadscreens/airdefense1"\r\n'

def ban_list(runtime: Path) -> bytes:
    """A local server's ban list as idGameLocal::SaveBanList writes it: each
    ban's name and GUID as little-endian length-prefixed strings."""
    def string(text: str) -> bytes:
        data = text.encode('utf-8')
        return struct.pack('<i', len(data)) + data
    bans = (('^1Griefer', 'ABCDEFGHIJK'), ('Camper', 'BCDEFGHIJKL'), ('Spammer', 'CDEFGHIJKLM'))
    return b''.join(string(name) + string(guid) for name, guid in bans)


# Files a scenario's game directory holds before it starts.
SEEDS = {'loadgame-failure': {'savegames/damaged.save': damaged_save,
                              'savegames/damaged.txt': lambda runtime: DAMAGED_DESCRIPTION.encode('utf-8')},
         'createserver': {'banlist.txt': ban_list},
         # The browser's favorites (one address a line): the scenario's server.
         'joingame': {'server_favorites.list': lambda runtime: f"127.0.0.1:{SCENARIO_SERVERS['joingame']['port']}\n".encode('ascii')},
         'join-details-view': {'server_favorites.list':
                               lambda runtime: f"127.0.0.1:{SCENARIO_SERVERS['join-details-view']['port']}\n".encode('ascii')},
         'join-view-glyphs': {'server_favorites.list':
                              lambda runtime: f"127.0.0.1:{SCENARIO_SERVERS['join-view-glyphs']['port']}\n".encode('ascii')},
         'joinfilters': {'server_favorites.list': lambda runtime: f"127.0.0.1:{SCENARIO_SERVERS['joinfilters']['port']}\n".encode('ascii')},
         'join-empty-clear': {'server_favorites.list':
                              lambda runtime: f"127.0.0.1:{SCENARIO_SERVERS['join-empty-clear']['port']}\n".encode('ascii')},
         'join-notices': {'server_favorites.list':
                          lambda runtime: f"127.0.0.1:{SCENARIO_SERVERS['join-notices']['port']}\n".encode('ascii')},
         'stockbrowser': {'server_favorites.list': lambda runtime: f"127.0.0.1:{SCENARIO_SERVERS['stockbrowser']['port']}\n".encode('ascii')},
         'savegame': {'savegames/auto.save': damaged_save,
                      'savegames/auto.txt': lambda runtime: AUTOSAVE_DESCRIPTION.encode('utf-8')}}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def to_png(tga: Path) -> Path | None:
    try:
        from PIL import Image
    except ImportError:
        return None
    png = tga.with_suffix('.png')
    Image.open(tga).convert('RGB').save(png)
    return png


def mod_manifest(runtime: Path) -> str:
    """The runtime's own manifest under a test name, so the mod matches its version."""
    manifest = json.loads((runtime / 'baseoq4' / 'mod.json').read_text(encoding='utf-8'))
    manifest['name'] = 'openQ4 retained fallback test'
    return json.dumps(manifest, indent=2) + '\n'


def start_server(args, output: Path, spec: dict, options: dict) -> tuple:
    """A scenario's dedicated server (SCENARIO_SERVERS), once it listens: on
    the loopback interface only, a LAN server (no master heartbeat), idling on
    a script that quits once rewritten."""
    game = output / 'server' / 'baseoq4'
    game.mkdir(parents=True)
    idle = game / 'server_idle.cfg'
    idle.write_text('waitMsec 250\nexec server_idle.cfg\n', encoding='utf-8')
    cvars = {
        'win_allowMultipleInstances' if os.name == 'nt' else 'sys_allowMultipleInstances': '1',
        'fs_basepath': str(args.assets.resolve()), 'fs_savepath': str(output / 'server'), 'fs_devpath': str(output / 'server'),
        'fs_game': 'baseoq4', 'com_gameMode': 'MP', 'logFile': '2', 'logFileName': 'logs/openq4.log',
        'net_ip': '127.0.0.1', 'net_port': str(spec['port']), 'net_serverDedicated': '1', 'net_LANServer': '1',
        'si_name': spec['name'], 'si_pure': '0', 'si_maxPlayers': '4', 'si_gameType': 'DM', 'si_warmup': '0', 'bot_minPlayers': '0',
        **spec.get('cvars', {}),
    }
    command = [str(args.runtime.resolve() / ('openQ4-ded_x64.exe' if os.name == 'nt' else 'openQ4-ded_x64'))]
    for key, value in cvars.items():
        command += ['+set', key, value]
    command += ['+spawnServer', spec['map'], '+exec', idle.name]
    stream = (output / 'server-process.log').open('w', encoding='utf-8')
    process = subprocess.Popen(command, cwd=args.runtime.resolve(), stdout=stream, stderr=subprocess.STDOUT, **options)
    log = game / 'logs' / 'openq4.log'
    deadline = time.monotonic() + 120
    while time.monotonic() < deadline:
        if log.is_file() and f"Server spawned on port {spec['port']}." in log.read_text(encoding='utf-8', errors='replace'):
            return process, stream, idle, log, command
        if process.poll() is not None:
            break
        time.sleep(0.25)
    process.kill()
    process.wait()
    stream.close()
    raise RuntimeError(f'the dedicated server for {output.name} never listened on port {spec["port"]}')


def stop_server(server: tuple) -> int:
    """Quit a scenario's dedicated server through its idle script."""
    process, stream, idle, _log, _command = server
    idle.write_text('quit\n', encoding='utf-8')
    try:
        returncode = process.wait(timeout=20)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        returncode = None
    stream.close()
    return returncode


def run(args, scenario: str) -> dict:
    output = (args.output / scenario).resolve()
    overrides_files = OVERRIDES.get(scenario, {})
    game_dir = FALLBACK_MOD if overrides_files else getattr(args, 'game_dir', 'baseoq4')
    game = output / 'save' / game_dir
    game.mkdir(parents=True)
    level, stages = SCENARIOS[scenario]
    if overrides_files:
        (game / 'mod.json').write_text(mod_manifest(args.runtime.resolve()), encoding='utf-8')
    for relative, content in overrides_files.items():
        target = game / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content(args.runtime.resolve()).encode('utf-8', errors='surrogateescape'))
    for relative, content in SEEDS.get(scenario, {}).items():
        target = game / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content(args.runtime.resolve()))
    # A scenario whose page launches a map continues once that map has loaded
    # (AFTER_MAP): the command buffer must be free for the launch meanwhile.
    after = None
    if AFTER_MAP in stages:
        split = stages.index(AFTER_MAP)
        stages, after = stages[:split], stages[split + 1:]
    finish = ['ui_retainedStatus', f'echo {DONE}', 'quit']
    lines = [f'echo RETAINED_STAGE {scenario}'] + stages + ([] if after is not None else finish)
    cfg = game / f'retained-{scenario}.cfg'
    cfg.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    chained = SCENARIO_FILES.get(scenario)
    if after is not None and chained:
        # The stage continues through chained files; the last one ends the run.
        names = list(chained)
        for name in names:
            lines_after = chained[name] + (finish if name == names[-1] else [])
            (game / name).write_text('\n'.join([f'echo RETAINED_STAGE {scenario} {name}'] + lines_after) + '\n', encoding='utf-8')
        after = None
    elif after is not None:
        after_cfg = game / f'retained-{scenario}-after.cfg'
        after_cfg.write_text('\n'.join([f'echo RETAINED_STAGE {scenario} after map'] + after + finish) + '\n', encoding='utf-8')
    width, height = args.size.split('x')
    overrides = {
        'fs_basepath': str(args.assets.resolve()), 'fs_savepath': str(output / 'save'), 'fs_devpath': str(output / 'save'),
        'fs_game': game_dir, 'logFile': '2', 'logFileName': 'logs/openq4.log',
        'r_fullscreen': '0', 'r_fullscreenDesktop': '0', 'r_borderless': '0', 'r_borderlessDefaultMigrated': '1', 'r_hiddenWindow': '1',
        'r_windowWidth': width, 'r_windowHeight': height, 'r_mode': '-1', 'r_customWidth': width, 'r_customHeight': height,
        'r_renderApi': args.renderer, 'r_multiSamples': '0', 'r_swapInterval': '1',
        'in_mouse': '0', 'in_joystick': '0', 'in_joystickRumble': '0', 's_noSound': '1',
        'com_skipLogoVideos': '1', 'g_autoSkipCinematics': '1', 'com_skipLoadingContinue': '1', 'com_maxfps': '60',
        'developer': '0', 'ui_autoJoin': '0', 'sys_lang': args.language,
        'ui_retainedTrace': '1', 'ui_retainedReducedMotion': '0',
    }
    if args.gate != 'default':
        overrides['ui_retained'] = args.gate
    # The harnesses that reuse run() with their own parsers may not offer --set.
    for item in getattr(args, 'set', None) or []:
        key, _, value = item.partition('=')
        overrides[key] = value
    overrides.update(SCENARIO_CVARS.get(scenario, {}))
    command = [str(args.runtime.resolve() / ('openQ4-client_x64.exe' if os.name == 'nt' else 'openQ4-client_x64'))]
    for key, value in overrides.items():
        command += ['+set', key, value]
    if level and level.startswith('mp:'):
        # A listen server the local player joins at once, as mp_control_smoke.py hosts.
        for key, value in {'com_gameMode': 'MP', 'net_serverDedicated': '0', 'net_LANServer': '1', 'si_pure': '0',
                           'si_maxPlayers': '4', 'si_gameType': 'DM', 'bot_minPlayers': '0', 'ui_autoJoin': '1',
                           'ui_spectate': 'Play', 'net_allowCheats': '1'}.items():
            if key not in SCENARIO_CVARS.get(scenario, {}):
                command += ['+set', key, value]
        command += ['+set', 'g_autoExecAfterMapLoad', cfg.name, '+set', 'g_autoExecAfterMapLoadDelayMs', '3000',
                    '+spawnServer', level[3:]]
    elif level:
        command += ['+set', 'g_autoExecAfterMapLoad', cfg.name, '+set', 'g_autoExecAfterMapLoadDelayMs', '3000', '+map', level]
    else:
        if after is not None:
            command += ['+set', 'g_autoExecAfterMapLoad', after_cfg.name, '+set', 'g_autoExecAfterMapLoadDelayMs', '3000']
        command += ['+exec', cfg.name]
    options = {'env': {**os.environ, 'TEMP': str(ROOT / '.tmp'), 'TMP': str(ROOT / '.tmp')}}
    if os.name == 'nt':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        options.update(startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    started = time.monotonic()
    # A held port stays on the loopback interface, which the engine's net_ip
    # names for that scenario.
    held = None
    if scenario in SCENARIO_PORTS:
        held = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        held.bind(('127.0.0.1', SCENARIO_PORTS[scenario]))
    server, server_result = None, None
    if scenario in SCENARIO_SERVERS:
        server = start_server(args, output, SCENARIO_SERVERS[scenario], options)
    try:
        with (output / 'process.log').open('w', encoding='utf-8') as process_log:
            process = subprocess.Popen(command, cwd=args.runtime.resolve(), stdout=process_log, stderr=subprocess.STDOUT, **options)
            try:
                returncode = process.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                returncode = None
    finally:
        if held is not None:
            held.close()
        if server is not None:
            server_returncode = stop_server(server)
            server_log = server[3]
            server_result = {'returncode': server_returncode, 'command': server[4],
                             'log': str(server_log.relative_to(ROOT)) if server_log.is_file() else None}
    log_path = game / 'logs' / 'openq4.log'
    log = re.sub(r'\^[0-9]', '', log_path.read_text(encoding='utf-8', errors='replace')) if log_path.is_file() else ''
    # A scenario whose page reloads the game module logs the reloaded module
    # to its own file (MODULE_LOG), which continues this one.
    module_log = game / 'logs' / MODULE_LOG
    if module_log.is_file():
        log += re.sub(r'\^[0-9]', '', module_log.read_text(encoding='utf-8', errors='replace'))
    shots = []
    for tga in sorted((game / 'screenshots').glob('*.tga')):
        png = to_png(tga)
        shots.append({'tga': str(tga.relative_to(ROOT)), 'png': str(png.relative_to(ROOT)) if png else None, 'sha256': digest(tga)})
    status = re.findall(r'OPENQ4_RETAINED .*', log)
    return {
        'scenario': scenario, 'gate': args.gate, 'renderer': args.renderer, 'size': args.size, 'returncode': returncode,
        'seconds': round(time.monotonic() - started, 1), 'complete': DONE in log, 'status': status,
        'retained_loaded': re.findall(r'RETAINED_GUI_LOADED (\S+)', log),
        'session_requests': re.findall(r'RETAINED_GUI_SESSION .*', log),
        'loading_phases': re.findall(r'RETAINED_LOADING_PHASE .*', log),
        'retained_warnings': [line for line in log.splitlines() if 'retained' in line.lower() and ('WARNING' in line or 'ERROR' in line)],
        'gui_values': re.findall(r'GUI_VALUE .*', log), 'system_reports': re.findall(r'OPENQ4_SYSTEM .*', log),
        'game_dir': game_dir, 'overrides': sorted(overrides_files),
        'errors': [line for line in log.splitlines() if 'ERROR:' in line or 'FATAL:' in line],
        'screenshots': shots, 'command': command, 'log': str(log_path.relative_to(ROOT)) if log_path.is_file() else None,
        'server': server_result,
        'host_input_injection': False, 'windowed': True, 'hidden_window': True,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, default=ROOT / '.install')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), default='gl')
    parser.add_argument('--size', default='1280x720')
    parser.add_argument('--gate', choices=('0', '1', 'default'), default='default',
                        help='ui_retained for the run; default leaves it unset')
    parser.add_argument('--language', default='english')
    parser.add_argument('--scenario', action='append', choices=sorted(SCENARIOS))
    parser.add_argument('--timeout', type=int, default=180)
    # On a shared machine, win_allowMultipleInstances=1 lets a run start beside
    # another openQ4 client without taking the single-instance lock from it.
    parser.add_argument('--set', action='append', default=[], metavar='CVAR=VALUE', help='set a CVar for every run')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('use a new output directory')
    if not args.output.resolve().is_relative_to((ROOT / '.tmp').resolve()):
        parser.error('captures belong under the repository .tmp directory')
    results = [run(args, scenario) for scenario in (args.scenario or list(SCENARIOS))]
    report = args.output / 'capture.json'
    report.write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    for result in results:
        print(f"{result['scenario']}: exit={result['returncode']} complete={result['complete']} "
              f"loaded={result['retained_loaded']} shots={len(result['screenshots'])} errors={len(result['errors'])}")
        for line in result['status']:
            print('   ', line)
        for line in result['loading_phases']:
            print('    ~', line)
        for line in result['retained_warnings'][:10]:
            print('    !', line)
    return 0 if all(result['complete'] and result['returncode'] == 0 for result in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
