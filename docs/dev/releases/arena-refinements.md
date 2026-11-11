# Arena Campaign refinements — pending release notes

> **Detailed change record.** Everything recorded here up to 11 November 2026 shipped in [0.14.1](v0.14.1.md), whose notes are the player-facing summary. Later additions are unreleased development notes. See [release status](../../user/release-status.md).

## Highlights

- **The match-start camera move and "Fight!" are back.** After the countdown
  the camera circles your fighter and settles into first person while the
  announcer counts "three, two, one"; "Fight!" now lands exactly as you get
  control. Previously the move was skipped and the match started with no call.
- **Proper opponent introductions.** Each fighter now gets a face-on shot and
  a named card that stays up for the whole beat. Team matches introduce your
  teammates first, then the opposition, and label each side
  ("TEAMMATE 1 OF 2", "OPPONENT 2 OF 3"). Introductions used to film fighters
  from behind, and with the modern HUD their card did not appear at all.
- **A real pause menu, like a story level.** Escape during an Arena match now
  opens **MATCH PAUSED** with **Resume**, **Restart match**, **Settings** and
  **Leave match**, beside the match, its tier, game type, map, clock and score,
  instead of the multiplayer menu's team, server and admin pages. The match
  pauses while it is open: bots, the match clock, projectiles and respawns all
  stop, including while you change settings. Restart match resets the same
  match on the spot, without reloading the map, and plays its introductions
  again; both it and Leave match ask first, and neither records anything. The
  classic and modern interfaces both have it.
- **Leaving returns you to the Arena.** Quitting a match part way through
  brings you back to the Arena browser with a "Match abandoned" report and a
  Retry button, instead of the title screen. Nothing is recorded and your
  progress is unchanged.
- **Skip the ceremony.** Press Escape — or fire, once the beat has had a moment
  on screen — to skip the remaining introductions or the end-of-match tableau,
  scoreboard and stats. Your result is already decided and is recorded as
  normal, and now it is also kept if you leave the match or quit the game
  during the ceremony.
- **Clearer cinematic framing.** The crosshair and status bar no longer sit on
  top of the introductions, countdown, spawn-in and victory shots. The match
  card now shows the tier and the match's place in it ("BOOT CAMP / QUALIFIER 1"),
  Duels are called Duels on the card, scoreboard and loading screen, and the
  scoreboard is headed by the match name rather than a server name and address.
- **Best results for every match.** The Arena browser now shows each match's
  best result across the foot of its picture: the outcome, the score and the
  difficulty you played it on. Replay a match and beat it, and the match report
  marks your score **NEW BEST**. A win beats a draw, a draw beats a defeat, and
  then the wider margin and the harder difficulty count. Matches won in an
  earlier version show **VICTORY** until you replay them. **Reset Progress**
  clears best results too, and it is now offered as soon as any match has a
  result, not only after a win.
- **Victory screen fixes.** The modern HUD's champion card now shows the
  winner's name. The victor is framed from the front, below the result and
  score rather than under them, and you can steer the orbit even when you were
  killed by the final frag. A draw is announced as a
  tie rather than "You lose".

## Fairness

- Removing the bots — from the console, the multiplayer menu's Admin page or
  Match Control — can no longer win a match. A match whose opponents all leave
  ends as **Match aborted** with nothing recorded, and the Admin page's kick,
  team and server-rule actions are refused during an Arena match.
- Tier briefings no longer name a tier's boss before you have revealed it by
  winning the three qualifiers.

## Smaller improvements

- Tiers with some qualifiers won now read **IN PROGRESS** in the browser.
- **NEXT** after a win selects the next match you still have to win; replaying
  a won match no longer points you back at a boss you have already beaten.
- A replayed win says it was already on your record instead of claiming the
  circuit opened up.
- When a match cannot start, its report now names the match as well as the
  reason.
- The stats screen at the end of a match no longer shows a chat entry line, a
  mouse cursor or "Press Escape to return to the game".
- After the final boss, later wins show their awards again; the "Campaign
  complete" banner is shown only on the win that completes the campaign. Long
  award lists wrap instead of being cut off.
- The match report can no longer be left as an empty frame by keyboard or
  controller navigation reaching the controls behind it, or by a quick click
  landing on the same frame as its closing animation.

## Upgrade Notes

- Replace the complete package: the engine and both game modules change
  together, and the updated menus, HUD and translations ship in `pak0`.
- Campaign progress, saves and settings are unaffected. Best results start
  recording with your next Arena match.
