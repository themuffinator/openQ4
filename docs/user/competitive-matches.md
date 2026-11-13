# Competitive matches

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

> **Experimental.** Multiplayer is still under active development. Expect rough
> edges, bugs and behaviour that changes between versions.

An openQ4 server runs either **casual** games, which play like stock Quake 4,
or **managed** competitive matches. In a managed match the server looks after
ready-up, team rosters and roles, timeouts, votes, best-of series with map
vetoes, and a saved match report with a multi-view demo. Players, captains,
referees and organisers all use it through the in-game **Match Control** page.

This guide starts with what you need as a player, then covers the match roles,
game rules, and setting up and running a managed server.

**For leagues:** managed matches are still experimental. Keep your usual way of
checking results rather than relying on openQ4's report alone.

## Play in a managed match

### Open Match Control

During a match, press Escape and open the **Match** tab (in the classic menus,
choose **Match Control**). It has six sections: **Status**, **Teams**,
**Proposals**, **Rules**, **Series** and **Evidence**. Each shows only what
your current role is allowed to see.

- An action you cannot use stays visible and says why when you point at it or
  move to it with the keyboard or a controller. Long explanations wrap and
  scroll.
- Actions that are hard to undo, such as forcing ready, committing rules,
  removing or substituting a player, cancelling a series, forfeiting and
  aborting, ask you to confirm.
- If the match changed while you were deciding, the server refuses the outdated
  request instead of acting on it.

The HUD and scoreboard also show a compact card with your role, the match
phase, any pause, readiness, timeouts left, open votes, series progress and any
item timers you are allowed to see.

### Get ready

Choose ready on **Status**, or use these console commands:

| Command | What it does |
| --- | --- |
| `ready` | Marks you ready. |
| `notready` | Withdraws your ready state. |
| `readyup` | Switches between ready and not ready. |

Readiness shows during warmup and countdown only. Spectators and bots never
count towards a managed match's ready check. If you withdraw during the
countdown and the match no longer has enough ready players, the countdown stops
and starts again once it does.

### Join a team or the Duel queue

On **Teams** you can join a side, spectate, or join, defer or leave the Duel
queue, and accept a roster invitation. Joining a team follows the server's
**Auto Balance Teams** setting, as the team menu does: a join that would put
that team two or more players ahead is refused with that reason, and you stay
where you are.

The Duel queue keeps arrival order. Joining it again does not move you
forward, and deferring moves you behind the current challengers. When you are
promoted to play, you still need to ready up. Disconnecting removes you from
the queue.

### Take a timeout

In a managed Duel, choose **Request timeout** on **Status** to spend one of your
timeouts (you appear as **Contestant A** or **Contestant B**), and **Resume** to
end your own pause. In team modes, captains call timeouts.

While the match is paused:

- chat, the scoreboard, Match Control and looking around keep working;
- movement, attacks, damage and the rest of the game stay frozen;
- the match clock and the respawn, item, powerup and other game timers stop,
  and pick up where they left off;
- play resumes after a countdown.

A timeout is charged once, when the pause starts, however many times it is
requested. A referee's technical pause does not use either side's timeouts.

### Vote on proposals

Managed matches use **proposals** instead of the old console-command votes. A
proposal is one match action from a fixed list, such as changing the rules or
aborting, never free-form console text. Vote yes, no or abstain on
**Proposals**, which shows who called the vote, the action, the time left, the
vote count, how many yes votes are needed and your own vote.

- Only active human players vote. Who can vote, and how many yes votes are
  needed, are fixed when the proposal opens, so players joining or leaving
  cannot change the threshold. Bots and ordinary spectators never vote.
- One match-wide proposal and one proposal per team can be open at the same
  time.
- **Forfeit (Side)** is the one team proposal: any active player can open it for
  their own team during live play. Only that team sees it, and a majority of
  the team's connected players must vote yes. If it passes, the team forfeits
  exactly as if its captain had chosen Forfeit. Referees and the other team
  cannot open it, and Duel has no team proposal.
- A proposal that fails, expires, is cancelled or no longer applies frees its
  place for another, after the normal cooldown.
- A passed proposal is checked again when it runs: the match phase, the
  caller's eligibility and the target must still allow it. It runs once, and
  fails if the caller has left.

### Spectate

While you spectate, **Status** has **Previous**, **Next** and **Free Camera**
buttons, or use these console commands:

| Command | What it does |
| --- | --- |
| `follow next` or `follownext` | Follow the next player you are allowed to watch. |
| `follow prev` or `followprev` | Follow the previous player. |
| `follow free` or `followfree` | Fly freely, if the server allows it. |
| `follow <client-slot>` | Follow the player now in that numbered slot. |

During a managed match's countdown and live play, ordinary spectators cannot
follow players. Coaches can follow their own side, and broadcasters and
referees have their own views. The server checks your permissions each time you
switch, and never sends information your role may not see. A request to
follow one player never ends up following someone else who has since taken
their slot. Bots can be followed. Demo playback has its own camera controls.

### See the result

At the end of a match the summary shows the outcome separately from the
scores: the winning player or team, a forfeit, a draw or an aborted match. A
forfeit awards the opponent even if the forfeiting side was ahead. Score rows
keep their original scores and order, and a draw or abort names no winner.

The winner is fixed when the match ends: later renames, disconnects, reused
slots and team changes cannot change it. **Status** shows the last result
through the review and the following warmup, until the next countdown starts.
It shows even when the server saves no match report.

## Roles

Roles belong to your current connection in the current match. Reconnecting, or
reusing a name, slot or IP address, never brings a role back. No role gives
remote console (rcon), ban-list or file access.

| Role | Who | What they can do |
| --- | --- | --- |
| Player | Anyone playing | Join a side or the Duel queue, ready up, take Duel timeouts, vote, and forfeit for their own side. |
| Captain | Assigned to the first roster seat of a team | Everything a player can, plus manage their team's roster, readiness and timeouts. |
| Coach | An observer assigned to one team | Watch their team, including its health and armour, during live play. |
| Broadcaster | A neutral observer granted by the host | Watch both teams, with item timers. Cannot change the match. |
| Referee | A neutral observer with the referee password | Run the match: force ready, pause, change rules, manage rosters, run series, record forfeits and aborts. |
| Server operator | Whoever runs the server | Choose the profile, set the referee password and map pool, and use a few server-only commands. |

### Captain

For a team match, the server operator first sets **Roster Size per Team** in
**Rules**. The first seat on each side is its captain seat. To appoint a
captain, select the player on **Teams**, choose **Captain** and use **Assign
role**. Role changes have a short cooldown; wait for the action to become
available again.

A captain can also:

- set team readiness and lock or unlock their side;
- invite, remove and assign roles to roster members, and swap in substitutes;
- keep substitutes on a bench without counting them as active players;
- call their side's timeout and agree to resume; and
- make their side's map-veto and starting-side choices in a series.

Invitations need the captain's authority to still hold when accepted. A captain
who disconnects, is demoted or is benched loses any invitations they sent.
Accept invitations and assign roles during warmup; substitutions also work
during the countdown. Captains cannot invite an ordinary spectator to watch
live play; give that person a coach or broadcaster role instead.

### Coach

A coach is assigned to one team by a captain or referee. Coaches do not play,
cannot ready up and cannot change rosters, pauses, rules or vetoes. A coach
cannot also be a player, broadcaster or referee.

A coach or substitute who is not playing can use **Leave roster** on **Teams**
to give up their own seat. It cannot remove anyone else.

### Broadcaster

A broadcaster is a neutral observer outside both rosters. They can watch both
teams' players and health, and in managed matches see timers for the major
items on the map. Those timers stop during pauses, and only broadcasters and
referees see them.

On a listen server, the host selects a spectator on **Teams** and chooses
**Grant broadcaster** or **Revoke broadcaster**. On a dedicated server, the
operator uses:

```text
matchBroadcaster <current-client-slot> <on|off>
```

For example, `matchBroadcaster 5 on` makes the person in slot 5 a broadcaster.
It refuses an active player, bot, roster member, team member, referee, coach or
an empty slot.

openQ4 does not yet have working Q4TV repeaters, delayed broadcasts or live
multi-view feeds. To keep a watchable copy of a match, use the server's
multi-view demo recording.

### Referee

A neutral spectator outside both rosters enters the referee password on
**Status** and chooses **Sign in**. The password is never sent as plain text,
shown to other players or written to reports, and repeated wrong guesses are
slowed down.

A referee can, when the match phase allows:

- force the ready check, which is recorded in the match report;
- call a technical pause and start the resume countdown;
- stage, check, commit or discard rule changes;
- make exceptional roster and role changes;
- moderate proposals;
- set up, start, advance or cancel a series; and
- record an abort or a forfeit.

Choose **Sign out** when you are done. Disconnecting or a new match also signs
you out.

## Game rules worth knowing

These apply to casual games unless marked as managed.

### Warmup and starting

- With warmup on, `si_minPlayers` counts active humans and bots together, and
  at least one human must be playing.
- Only humans count towards the ready percentage (`si_warmupReadyPercentage`).
  Duel needs both seated players ready; waiting spectators do not count.
- Team games need players on both sides. With `si_teamForcePresent 1` both
  teams need `si_teamSizeMin` players; with `0`, one side must reach it. Red
  Rover handles its own round rosters.
- `si_useReady 0` starts as soon as there are enough players, and `si_warmup 0`
  skips warmup for practice. A `si_warmupReadyPercentage` of `0` also turns off
  the ready percentage outside Duel; Duel still needs both players ready when
  ready-up is on.

### Ending a match

- A tie at the frag or capture limit goes to sudden death. A tie when the clock
  runs out gets overtime. If both teams pass the score limit, the leader wins.
- Losing all the opponents ends live play, even with no time limit. If the last
  human leaves, the match is aborted and any bots wait for players.
- Tied scores share a place: `10, 10, 9` is first, first, third.

### Duel

Duel is winner-stays. Only the two seated players are ranked; the queue stays
in the spectator list. Both finalists stay on the final scoreboard until the
review ends, then the loser goes to the back of the queue and the next
challenger takes the seat.

In a managed Duel, a forfeit retires the player who forfeited even if they were
ahead, and a tie sends both finalists back to the queue. Players promoted from
the queue must ready up. During a best-of series the same two players stay
seated between maps, and aborting a match keeps both of them and the queue.

### Voting on casual servers

Turn on option voting with `si_allowVoting 1`. Only active human players call
and cast votes; bots do not, and they do not raise the number needed to pass.
Who can vote is fixed until the vote ends, even if players disconnect.

### Round modes

- Clan Arena, Freeze Tag and Red Rover reset the map between rounds while the
  match carries on.
- **Freeze Tag:** frozen bodies stay even after heavy damage. Stand within
  `si_freezeThawRadius` of a frozen teammate, with a clear view of them, for
  `si_freezeThawTime` seconds to thaw them and score a point. Stepping away
  undoes some of the progress. `si_freezeThawThroughSurface 1` lets you thaw
  through walls.
- **Red Rover (managed):** a defeated player moves to the other side for the
  rest of the round without counting as a team change. Roster seats and captain
  roles stay, and the next round puts everyone back on their home side. Team
  text and voice chat follow the side a player is on now.

### Objective modes

- **DeadZone:** control time counts only during live play. Dead or spectating
  players, and players without a required token, cannot hold or contest the
  zone. Any eligible opponent in the zone contests it, whoever entered first.
- **One Flag** modes can use the original CTF maps. On a map with both team
  bases but no neutral flag, openQ4 places the neutral flag at the spare pickup
  spot nearest to both bases. A neutral flag placed by the mapper, or a map's
  own One Flag layout, always comes first. Check flag placement when you choose
  a competition map pool.

## Run a managed server

### Choose a profile

Set up a server as in the [server setup guide](server-setup.md), then choose a
profile with `g_matchProfile` before the match starts. This example uses only
stock Quake 4 content:

```text
openQ4-ded_x64 +set si_name "Managed Duel" +set si_map mp/q4dm1 +set si_gameType Duel +set g_matchProfile competitive_duel +spawnServer
```

| Profile | Modes | Main rules |
| --- | --- | --- |
| `casual` | Every available mode | The default. Flexible server settings, the team menu and the old votes; no managed-match restrictions |
| `competitive_dm` | DM | Every human must ready up; no frag limit |
| `competitive_tourney` | Tourney | Every human must ready up for the tournament arena |
| `competitive_duel` | Duel | Two active humans, both ready; two 60-second timeouts each |
| `competitive_tdm` | Team DM | Every human ready; friendly fire, 15 minutes, 50 frags, two 60-second timeouts per side |
| `competitive_ctf` | CTF family | Every human ready; friendly fire, 20 minutes, five captures, two 60-second timeouts per side |
| `competitive_deadzone` | DeadZone | Every human ready; friendly fire, 15 minutes and 120 seconds of control |
| `competitive_round` | Clan Arena, Freeze Tag and Red Rover | Every human ready; eight rounds, three-minute rounds, sudden death |

A profile that does not suit the game type is replaced by that game type's
recommended profile. The server publishes the committed profile in the
read-only `si_matchRules` server-info value; its code identifies the rules and
is not a password or proof of who runs the server.

Managed matches support:

- DM and Tourney;
- Duel;
- Team DM;
- CTF, One Flag CTF, Arena CTF and Arena One Flag CTF;
- DeadZone; and
- Clan Arena, Freeze Tag and Red Rover.

Overload, Harvester, Domination and Attack & Defend are not complete, so they
are hidden from hosting, voting, the browser filters and map checks.

The **Rules** section separates **Minimum Active Players** (bots included) from
**Minimum Active Humans**.

### What changes in a managed match

- The old console-command votes are refused; any vote already open, or passed
  and waiting to run, is cancelled when the managed match takes over. Use
  proposals instead.
- The old ways to change server settings, shuffle or kick players and force
  team changes are blocked. Change rules, rosters and players through Match
  Control.
- Banning and unbanning stay with the normal server administration (rcon); see
  [server security](server-security.md). A profile never grants rcon, exposes
  server files or needs replacement Quake 4 content.

### Set a referee password

If you want remote referees, set `g_refPassword` in a protected server
configuration or at the server's own console before the match starts. The game
keeps only a check value made from it and clears the setting when the match
begins. It is not the rcon password; do not reuse your administration password.

### Set the map pool for a series

List the maps in `si_mapCycle`, separated by semicolons:

```text
set si_mapCycle "mp/q4dm1;mp/q4dm3;mp/q4dm5;mp/q4dm7;mp/q4dm9"
```

The server removes duplicates and checks that each map is installed and suits
the game type. If the list is empty, it builds a pool from the installed maps
that suit the game type. Map-cycle scripts in the game folder can include other
scripts by relative path on every platform.

### Server operator commands

On a listen server, the host gets operator actions in Match Control. A
dedicated server operator uses these console commands, which follow the same
checks as Match Control:

| Command | What it does |
| --- | --- |
| `forceReady` | Forces the match through the ready check and starts the countdown. |
| `matchBroadcaster <slot> <on\|off>` | Grants or removes the broadcaster role. |
| `matchSeriesBind <a\|b> <slot>` | After a Duel series is restored, binds each current player to their series seat. |

## Series and map vetoes

Match Control runs best-of-one, best-of-three and best-of-five series for Duel
and team modes. A referee sets the series up on **Series**; the server checks
the profile, the two contestants, the game type, the map pool and the veto
order together.

- Each ban, pick, decider and starting-side choice is taken in turn. In team
  modes the turn belongs to a team's series side, as Match Control shows it,
  even when the teams have swapped Marine and Strogg for a map.
- When the veto is done, the first map loads automatically. After each map's
  review, **Advance map** loads the next one, or ends the series once a
  contestant has enough wins.
- Only a decided result or a forfeit scores a series point; an abort does not.
- If a chosen map is missing before it loads, the attempt is recorded and the
  series waits to retry. If the map loads but fails, for example because its
  file is damaged, the next match on any other map records it as an aborted
  attempt with no score; Match Control shows it as a review, and **Advance map**
  tries the same map again once it is fixed. The same happens if the server
  restarts mid-map and comes back on a different map. Each aborted attempt
  counts towards the series attempt limit.

### Recover a series after a restart

The server saves every series change to a recovery file in your save path:

```text
baseoq4/match-series/series-<series-id>.oq4series
```

The series score and its report are saved together, so one can never get ahead
of the other. While a series runs, the server keeps its identity in
`g_matchSeriesRecoveryId` and clears it when the series ends or is cancelled.
On the next map or server start, openQ4 checks the file against the rules, game
type, current map and map pool before restoring the series.

- Do not edit the file or the setting during a series. To abandon a series that
  cannot be recovered, set `g_matchSeriesRecoveryId 0` and keep the old file for
  investigation.
- If the server stopped after a completed map, Match Control shows that map's
  review in warmup. Use **Advance map** to continue.
- Team series reconnect to the current teams automatically.
- **Duel** recovery stores no names, addresses or slots, so in warmup the
  operator binds both current players to their seats, using the server's
  current player list:

  ```text
  matchSeriesBind a 3
  matchSeriesBind b 7
  ```

  Each binding lasts only for that connection. The command works only in Duel
  warmup with a restored series, for a current active human player.

## Match reports and demos

### Match reports

A managed match can save a report for each map. `g_matchEvidence` chooses how
much:

- `0`: no report file (a series still keeps the result in memory for its own
  report);
- `1`: the final report for each map;
- `2` (the default): the final report plus a full log of the match's events.

Reports are saved in your save path:

```text
baseoq4/match-results/session-<session>_series-<series>_<map>.json
```

A report includes the build, map, game type, rules, series link, match events
(phases, pauses, roster and role changes, proposals), final player and team
statistics, the result and the path of the match's demo. It never includes IP
addresses, passwords or login details. A report that fails to save never
changes the winner.

When a series ends or is cancelled, openQ4 also saves a series report with the
contestants, every map attempt and its result, the combined statistics and
which reports and demos are available:

```text
baseoq4/match-results/series-<series-id>.json
```

### Automatic demos

A managed match starts recording a [multi-view demo](multiview-demos.md) when
the countdown begins and stops it once the result is saved. If an operator
already started a recording, the match leaves it alone.

- A recording still open when a map ends has no playable link in that map's
  report. Stop it before the series ends and the series report links the
  finished file; if it is still open when the series ends, the series report
  stays marked as pending.
- The **Evidence** section shows whether a report and demo are starting,
  recording, finished or saved. A file path on screen does not by itself mean
  the file was saved.
- Finished `.mvd` demos appear in **Multiplayer → Demos**; see
  [Watch a demo](multiview-demos.md#watch-a-demo). A `.mvd.part` file is an
  unfinished recording, not a match record.
- If a report or demo fails, keep the log and the partial files for
  investigation rather than renaming a partial file.

## Compatibility and upgrading

- The server and every player need the same openQ4 version (0.14.1).
- Finish any managed series before upgrading openQ4. Rules and series saved by
  an older version may not match, and some older recovery files can be read for
  investigation but not resumed.
- Multiplayer demos recorded by older development versions may play only in the
  version that recorded them.
