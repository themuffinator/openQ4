# Multiplayer human qualification checklists

These are the MP-01, MP-04, MP-06, MP-07, MP-08 and MP-09 gates that need a
person at the controls or physical hardware. Automated fixtures may never
synthesize keyboard, mouse or controller input, so these steps cannot be run
by an agent. Everything else under those items has executable or loopback
evidence; see [the roadmap](roadmap.md#mp-task-evidence-and-acceptance).

Each checklist names the build to use, the exact setup, what to do and what
counts as a pass. Record a result as one line per step: pass, fail (with what
you saw) or not run. The log is written to
`<fs_savepath>\baseoq4\logs\openq4.log`. With the VS Code launch
configurations that is `.home\baseoq4\logs\openq4.log`.

## Common setup

- Stage the current build into `.install/` and launch from there:
  `tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects`.
- Note the build banner at the top of the log (`0.13.2-dev+g<sha>`) in your
  report, so the result binds to a build.
- Two-machine steps: both machines run the same staged package. A second
  local client can be started with `+set win_allowMultipleInstances 1` and its
  own `+set fs_savepath <folder>`.
- Run every menu step once with keyboard and mouse, and once with a
  controller only. Run every step at 16:9 and at 4:3, with the retained
  interface (`ui_retained 1`, the default) and once with English plus one
  other language (`sys_lang`).

## MP-01: competitive Match Control by hand

Host a listen server with `+set si_gameType "Team DM" +set g_matchProfile
competitive_tdm +set g_refPassword <password> +spawnServer mp/q4dm1`. Join it
with two more clients, so there are three humans. Open Match Control from the
Escape menu each time.

| # | Role | Do this | Pass when |
| --- | --- | --- | --- |
| 1.1 | Any player | Move focus through every Match Control tab and row, then back out, using only the keyboard; repeat with only a controller. | Every tab and row is reachable, focus is always visible, and Back closes one level at a time. |
| 1.2 | Any player | Focus an action that is unavailable (for example Forfeit in warmup). | It is shown disabled with a readable localized reason; activating it sends nothing. |
| 1.3 | Spectator | Type the referee password into the credential field and sign in; sign out; reopen the field. | Sign-in works, the field is empty after submit and after reopening, and the password never appears in the log. |
| 1.4 | Host | Assign both captains; as a captain, invite the third player, who accepts. | The invitation, acceptance and roster change appear for everyone within a second. |
| 1.5 | Captain | Ready the team; un-ready during the countdown. | The countdown starts, then cancels back to warmup. |
| 1.6 | Captain | During live play request a timeout, then resume. | One pause, one timeout charged, the resume countdown runs. |
| 1.7 | Strogg player (non-captain) | During live play open Proposals, choose **Forfeit / Side** and create it. Then, as the Strogg captain, vote **No**. | Only Strogg players see the ballot; the Marine host does not. It fails on the No and the match continues. |
| 1.8 | Strogg player | Wait 30 seconds, create **Forfeit / Side** again; the Strogg captain votes **Yes**. | The match ends as a Strogg forfeit, won by Marine. |
| 1.9 | Referee | Follow a player, have that player disconnect and reconnect. | The camera lets go on disconnect and does not jump back to the reconnected player. |

## MP-04: Arena by hand

Start Arena from the main menu (any card).

| # | Do this | Pass when |
| --- | --- | --- |
| 4.1 | Pause during a round with Escape, then move through every pause-menu row with the keyboard, then with a controller; resume. | Every row is reachable; world sound stops while paused and resumes on unpause. |
| 4.2 | Choose Restart match from the pause menu. | The match restarts in place within about a second; bots keep their slots. |
| 4.3 | With headphones or a USB headset as the default output, unplug it during a round, then plug it back in. | Sound moves to the next device (or stops cleanly) and comes back on reconnection, with no crash or freeze. |
| 4.4 | Repeat 4.3 while paused, then resume. | Sound stays silent while paused and resumes on the restored device. |
| 4.5 | Win a match, then quit from the ceremony. Relaunch and look at the card. | The win and best result are kept. |

## MP-06: swimming and liquid effects by eye

Host `+set si_gameType DM +spawnServer mp/liquid_lab` (the liquid lab is in
the staged package). Optionally add `+set bot_minPlayers 4` for traffic.

| # | Do this | Pass when |
| --- | --- | --- |
| 6.1 | Walk into each pool (water, slime, lava) and swim around, down to the floor and back up. | Swimming controls work in all directions; you can reach the floor and climb out at the edge. |
| 6.2 | Stay under water until the AIR gauge empties. | The gauge drains, you take drowning damage at zero, and it refills on surfacing. |
| 6.3 | Watch the surface as you, then a bot, enter and leave the water at speed. | A splash plays at the surface each time, with its sound. |
| 6.4 | Stand waist-deep and walk; then stay submerged. | Wading ripples and sound play while walking; bubbles rise while submerged. |
| 6.5 | Fire rockets and hyperblaster into and out of the water. | Each shot that crosses the surface splashes there and continues. |
| 6.6 | Duck your head under and lift it out. | The underwater view tint and muffled sound switch on and off with the eye position. |

## MP-07: weapon visuals under prediction

Host a listen server on `mp/q4dm1` with `+set bot_minPlayers 3`, and join
from a second machine or client. Run the steps first with defaults, then
with `net_mpPredictMode 1` and `net_mpLagCompensation 1` set on both ends.
Add `+set net_forceLatency 80 +set net_forceJitter 20` on the client for the
impaired pass.

| # | Do this | Pass when |
| --- | --- | --- |
| 7.1 | Fire the machinegun, railgun and lightning gun while turning quickly. | The tracer, beam and impact start at your gun and land where the crosshair was; no visible lag or offset. |
| 7.2 | Use the gauntlet on a moving bot. | Sparks appear on the bot, not behind it. |
| 7.3 | Watch a remote player (the other client) fire hitscan and lightning at bots. | Their beams and impacts line up with their weapon and target. |
| 7.4 | Teleport, die and respawn while firing. | No beam or tracer is drawn from your old position. |

## MP-08: real network conditions

Use two physical machines.

| # | Do this | Pass when |
| --- | --- | --- |
| 8.1 | Lower one machine's interface MTU to 1280 (or connect over a VPN with a small MTU). Join a 12-player match on q4ctf1. | The client joins and plays for five minutes without a reliable-message overflow or disconnect. |
| 8.2 | Join the server by a hostname that resolves to two or more addresses, one of them unreachable (for example an A and an AAAA record where IPv6 is blocked). | The client tries the next address after three unanswered challenges and joins. |
| 8.3 | Run a dedicated server on one machine with 16 bots and two human clients on the other. | Both clients hold the game for ten minutes without resyncs piling up. |
| 8.4 | The Steam 1.4.2 `Quake4.exe` needs administrator rights, so the harness cannot start it. From an elevated prompt run `python tools/tests/mp_retail_protocol_smoke.py --retail-client-binary --basepath "<Quake 4 folder>" --output-dir .tmp/mp08-retail`. A Quake 4 window opens briefly and the harness closes it. | The report passes and `retail_client.binary.refusal_logged` is true: the retail client logged `server uses protocol 2.41` and never joined. |

## MP-09: bot difficulty against people

Host `+set si_gameType DM +spawnServer mp/q4dm1` alone, then `addbot <name> 1`
(or `5`). Play to 15 frags. Repeat with a newcomer and an experienced player.

| # | Do this | Pass when |
| --- | --- | --- |
| 9.1 | A player new to the map plays one skill-1 bot. | The player wins comfortably and the bot visibly misses and hesitates. |
| 9.2 | An experienced player plays one skill-5 bot. | The match is hard: the bot punishes standing still and straight runs, but strafing and cover make it beatable. |
| 9.3 | Same player plays skill 3. | It feels like a competent regular, clearly between 1 and 5. |
| 9.4 | For each match, write down the final score, and whether the bot felt fair (no wall-hacks, no instant perfect turns). | Notes are attached to the report; any unfair behaviour is described concretely. |

Report results against the roadmap item. A failing step reopens or extends
that item with the observed behaviour. A passing step closes only that
physical gate; it does not stand in for any other one.
