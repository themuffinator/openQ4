# Quake Live multiplayer port

openQ4 treats Quake Live as Quake 4's natural multiplayer successor. This
document records what was carried over from Quake Live, how it maps onto the
Quake 4 multiplayer code, and what is deliberately out of scope.

Game-side sources live in `openQ4-game/src/mpgame`. Quake Live behaviour was
read from the reconstruction in `QuakeLive-SRP/src/code`, never from its docs,
which disagree with its own registration tables in several places.

## Out of scope

Factories and rulesets are not ported. Quake Live expresses most of its
configuration through those two systems; openQ4 keeps plain `si_*` server
cvars instead, which is what Quake 4 servers already understand.

Race is not ported. It stores a millisecond time in the score field where
lower wins, which `mpPlayerState_t::fragCount` cannot represent (it is clamped
to -100..999), and it shares no mechanics with any other gametype.

Timeouts and pausing are deferred. Quake Live pauses by sliding every entity's
`nextthink`; idTech4 uses `idClass` event queues instead, so this needs a
pause-offset audit of every stored deadline in the multiplayer code and is
better done on its own.

## Gametype descriptor table

`mp/GameTypes.{h,cpp}` replaces what used to be a bare enum plus six
independent hand-written string switches (`SetGameType`, `si_gameTypeArgs`,
`GetLongGametypeName`, `VoteGameTypeToString`, `GameTypeToVote`,
`LocalizeGametype`) and three overlapping hard-coded predicate sets that
disagreed with each other — One Flag CTF counted as a team game in
`idGameLocal::IsTeamGame` but not in `IsTeamGameType`.

A gametype now declares itself once: name, abbreviation, `#str_` display name,
entity filter, map decl key and behaviour flags. `MPValidateGameTypeTable()`
runs at game init and fails loudly if the table, the enum and the
`si_gameType` completion list have drifted apart.

`gameType_t` is the first byte of every gamestate packet and is compared
literally by the `.gui` files, so it is **append only**.

### Playing new modes on stock maps

Each new gametype borrows the entity layout of the Quake 4 mode it is shaped
like, through the table's `entityFilter` and `mapDeclKey` columns. Clan Arena,
Freeze Tag and Red Rover borrow Team DM; Duel borrows DM; the objective modes
borrow CTF. That is why they need no new map content and are playable on the
shipped Quake 4 multiplayer maps.

## Match progression

| Quake Live | openQ4 |
| --- | --- |
| `level.warmupTime == -1` | `WARMUP` |
| `level.warmupTime > 0` | `COUNTDOWN` with `nextStateTime` |
| `level.warmupTime == 0` | `GAMEON` |
| `level.intermissionQueued` | `GAMEREVIEW` |
| `ExitLevel` | `NEXTGAME` |

Quake 4's seven-value `mpGameState_t` is kept as the canonical phase model; it
is already closer to what players see than Quake Live's single tri-state
integer.

**Overtime** (`si_overtime`, seconds) follows Quake Live's accumulator model:
a tie at the limit adds time to the same clock rather than starting a second
one, and every limit check measures against
`idMultiplayerGame::GetMatchLengthMsec()`. The fields ride the gamestate base
header, so the HUD clock is right on clients too. Quake 4's `SUDDENDEATH`
phase is kept intact as the fallback when a server sets `si_overtime 0`, so
the meaning of that already-shipped state byte is unchanged.

**Sudden death** in the Quake Live sense is a respawn penalty rather than a
phase: while a match is in overtime the respawn delay grows
(`si_suddenDeathRespawnDelay`, `si_suddenDeathRespawnIncrease`,
`si_suddenDeathRespawnMax`).

**Mercy rule** (`si_mercyLimit`) and **forfeit** (`si_forfeit`) are new. The
forfeit path also fixes a pre-existing hole: `CheckAbortGame` required both an
empty server *and* an expired time limit, so a drained server with
`si_timeLimit 0` sat in `GAMEON` forever.

## Round layer

`mp/RoundGameState.{h,cpp}` adds `rvRoundGameState`, an intermediate class
between `rvGameState` and the round based gametypes. The round machine is a
separate state field rather than extra `mpGameState_t` values, because
`GameTime()`, `ScheduleTimeAnnouncements()`, `CheckRespawns()` and
`UpdateHud()` all branch on `currentState`. `rvTourneyArena` already proves
the separate-field shape works.

Round state is delta-packed in its own tag stream after the base header, so no
existing gametype's wire format changes.

The layer provides: round countdown and result delays, round time limit with
players-left then total-health tiebreaks, round-locked respawn, attack lockout
during countdown and result, round win scoring, draws (which Quake 4 had no
concept of anywhere), and per-round announcer and banner messages.

Three new `rvGameState` virtuals support it: `AllowRespawn`, `PlayerDeath` and
`PlayerDamage`. Quake 4 had no way for a game state class to observe a death
or veto a respawn, which is why elimination rules previously had nowhere to
live outside the tourney arena special case.

## Warmup and ready-up

Ready state is now authoritative on the server, set by a new
`GAME_RELIABLE_MESSAGE_READY` client message and the `ready`, `notready`,
`unready`, `readyup` and `allready` console commands. Quake 4 carried ready in
the `ui_ready` userinfo key, which `ThrottleUserInfo` caps at one change every
five seconds, so a mistimed ready press was simply swallowed.
`idPlayer::readyUserInfo` shadows the last userinfo value so a stale resend
cannot undo a reliable update.

`AllPlayersReady` now uses Quake Live's ratio threshold
(`si_warmupReadyPercentage`, default 0.51) rather than requiring every single
connected player, with Duel demanding both players. Team modes additionally
require `si_teamSizeMin` players per side, gated by `si_teamForcePresent`.

`si_useReady` now defaults to **1**. With it off, `idPlayer::IsReady` is
unconditionally true and warmup ends the instant two clients connect.

Warmup gameplay follows Quake Live: every weapon with full ammo
(`si_warmupWeapons`) and no scoring (`si_warmupScoring`). Quake 4 previously
let frags accumulate through warmup and zeroed them on `GAMEON`, so the warmup
scoreboard showed a meaningless race. Changing sides during warmup now
withdraws your ready.

## HUD messages

Quake 4 has **no server-driven centre message at all**. `GAME_RELIABLE_MESSAGE_DB`
and `GAME_RELIABLE_MESSAGE_PRINT` both terminate in the four-line chat stack;
every big centre message was derived client-side from an observed state delta,
which cannot express text that varies with who did what to whom.

`GAME_RELIABLE_MESSAGE_CENTERPRINT` adds that channel. The payload is a
`#str_` id plus up to two typed parameters (`CPARM_INT`, `CPARM_CLIENT`,
`CPARM_TEAM`), so nothing pre-translated goes over the wire and player and
team names resolve on the receiving client with the right team colour. This
satisfies the project's rule that no display string may be hardcoded.

Format strings in the `#str_413xx`–`#str_414xx` block may use `%s` only —
every parameter is pre-formatted to text before `va()` sees it.

Announcer events for the new modes are **aliases onto stock Quake 4 clips**
(`AS_ROUND_FIGHT`, `AS_ROUND_YOU_WIN`, `AS_MATCH_OVERTIME`, …) rather than new
`announcerSound_t` values. Quake 4 ships no round or overtime voice-overs, and
`announcerSoundDefs[]` is index-parallel with no compile-time check, so
aliasing keeps openQ4 running on retail assets and avoids the easiest way to
break that table. Point the aliases at real shaders if a voice pack ships.

New HUD state keys written by `UpdateHud`: `overtime`, `overtimecount`,
`overtimetext`, `showready`, `readycount`, `readytotal`, `readytext`,
`showround`, `roundnumber`, `roundtext`, `roundtime`, `showroundtime`.
`guis/mphud.gui` renders them from the `oq4_*` windowDefs.

## Gametypes

| Mode | Status | Notes |
| --- | --- | --- |
| Duel | Implemented | 1v1 with a spectator queue, winner stays on. Kept separate from Quake 4's Tourney, which is a multi-arena elimination bracket and a genuinely different game sharing a name. |
| Clan Arena | Implemented | Round based team elimination. The loadout is granted on spawn from `GTF_FULLARSENAL`, not on the round-live edge, so a player is armed through the countdown; map pickups are suppressed for the same flag, because a mode where everyone starts with everything is not played over the item layout. Personal score is damage dealt plus frags, exactly as Quake Live's `PERS_SCORE` is. |
| Freeze Tag | Implemented | Death freezes; a team mate standing within `si_freezeThawRadius` for `si_freezeThawTime`, **with line of sight** (`si_freezeThawThroughSurface`), thaws you back in where you fell. Frozen bodies are invulnerable and thaw on their own after `si_freezeAutoThawTime`, or `si_freezeWorldDeathDelay` when the world killed you rather than an enemy. The body is the dead player themselves — Quake 4 already leaves the corpse in place and already replicates it, so no new entity is needed. |
| Red Rover | Implemented | Dying puts you on the other side — any death, including suicides and world deaths, which is what Quake Live's `G_RRHandlePlayerDeath` does. The round ends when one side has absorbed everybody. Round limit counts total rounds played, as in Quake Live. The infection variant is not ported. |
| One Flag CTF | Activated | Was already implemented and parseable but missing from `si_gameTypeArgs`, so it could not be selected or voted. Arena One Flag CTF likewise. |
| Overload | Not implemented | Needs a damageable team obelisk. |
| Harvester | Not implemented | Needs the obelisk plus a carryable skull item and a carried-count field. `teamFragCount` is already triple-purpose and must not take a fourth meaning. |
| Domination | Not implemented | Can reuse the existing `rvCTF_AssaultPoint` entities as control points; needs proximity capture rules and a per-point score tick. |
| Attack & Defend | Not implemented | Needs alternating attack turns on top of the round layer plus the one-flag machinery. |

The four remaining modes need new networked objective entities or new capture
rules; their cvars (`si_obeliskHealth`, `si_skullTimeout`, `si_domCaptureTime`,
`si_domScoreRate`, `si_scoreLimit`) and gametype table rows are already in
place.

## Bugs fixed along the way

- `riDZGameState` assigned `GS_DZ` to the **inherited** `rvGameState::type`
  because it declared no static of its own, corrupting the RTTI tag of every
  game state class process-wide after a DeadZone match.
- `SelectSpawnPoint` had two hard-coded gametype lists and no fallback, so any
  gametype named in neither hit a fatal error the first time a player spawned.
- Team DM's frag-limit-cancel branch left `fragLimitTimeout` armed on the
  non-tied path, so a leader who suicided during `FRAGLIMIT_DELAY` ended the
  match anyway.
- `AddTeamScore` was unclamped while `teamScore` ships as a `short`.
- `english_openq4.lang` and its three mirrors had a stray entry after the
  closing brace that the lexer never read.

## Second pass, audited against Quake Live

A later audit read every mode back against `QuakeLive-SRP` and found that the
port's *structure* was right but several of its *rules* were not, and that some
Quake 4 machinery the round layer leans on does the wrong thing for a mode that
was never in Quake 4. What that pass changed:

**Ending the match by accident.** `SwitchToTeam` calls `CheckAbortGame`, and
`EnoughClientsToPlay` fails a team game the moment one side is empty — which in
Red Rover is the win condition. Red Rover therefore ended the whole match on its
first completed round. `EnoughClientsToPlay` and `ForfeitTeam` now exempt
`GTF_TEAMSWAP`. `VerifyTeamSwitch` does too: with `si_autoBalance` on (its
default) it was rewriting every conversion to the *smaller* side, inverting the
mode's only rule.

**Leaving a round you were losing.** `AllowRespawn` only refused a player who had
been eliminated, so joining mid-round, or changing sides while cornered, put a
fully armed body into a round that was already half decided.
`rvRoundGameState::SealRound` now closes the roster at the RS_ACTIVE edge —
every slot that is not a live participant, empty slots included, so a client
connecting into a free slot inherits the seal — and a new `PlayerWithdrew` hook
tells the game state about the nodamage kill that `SwitchToTeam` uses, which
never reaches `idPlayer::Killed` and so was invisible to `PlayerDeath`.

**Corpses that counted as survivors.** `GiveStuffToPlayer` writes health with no
death test, so Clan Arena's round-start top-up resurrected anyone who had died
during the countdown into an unplayable body that `PlayerIsAlive` counted for
its team — a wiped-out side that could not lose the round. The top-up now skips
the dead, and `ReviveForRound` forces a real respawn for anyone dead but not
eliminated, so a round never opens with a body on the field.

**Freeze Tag.** Thawing ran a full `SelectSpawnPoint` and *then* teleported the
player to the body, firing an unrelated spawn point's targets, playing the spawn
effect over there, and kill-boxing twice — the second time on top of the team
mate who had just spent two seconds thawing. It is now one placement, at an
occupancy-tested spot near the body, falling back to an ordinary spawn point
when the body is somewhere nobody can stand. Thawing also required no line of
sight (through floors and walls), there was no auto-thaw of any kind so a body
in a pit removed a player for the whole round, and the 1 Hz progress notice was
an instruction sent to the player already following it.

**Duel.** The warmup ready gate counted the waiting queue as players who had to
ready up, and queued players cannot ready — so a duel with four or more
connected clients could never start. The queue also propped up the population
check, so a contender walking out left the server running an abandoned 1v0
instead of forfeiting and seating the next challenger. Queued players were being
fully spawned and re-spectated every respawn cycle, one spawn point and one kill
box at a time; `rvDuelGameState::AllowRespawn` now vetoes them instead.

**Presentation.** `mpmain.gui` offered no team buttons in Clan Arena, Freeze Tag
or Red Rover and routed Duel to the Tourney panel; `mphud.gui` showed no team
score in the round modes and coloured names by team in Duel but not in the three
modes that have teams. The scoreboard labelled Clan Arena's column "Damage" for
a number that has never been damage alone. The in-game server info line reported
`si_fragLimit` for modes scored by rounds.

**A server-driven announcer.** `ScheduleAnnouncerSound` queues into a client
local list and needs a local player, so every cue decided by server-only game
logic was silent on a dedicated server and played for the host regardless of who
it was about. `GAME_RELIABLE_MESSAGE_ANNOUNCER` (append only, like the rest)
gives the round layer a real channel; the last-one-standing cue uses it.

**Rule corrections read back out of Quake Live.** Clan Arena credits
`take + asave` capped at what the target had left, not the raw damage roll
(`G_CAHandleDamageScore`). Red Rover converts on every death, not just enemy
kills (`G_RRHandlePlayerDeath`), and its between-round reshuffle now runs for the
first round too. `GTF_BUYING` is finally tested rather than being dead metadata,
so the buy menu no longer appears in the four modes added since.

## Validation

- Dedicated server load on stock maps: Clan Arena (q4dm1), Duel (q4dm1),
  Freeze Tag (q4dm2), Red Rover (q4dm3), One Flag CTF (q4ctf1), Team DM
  (q4dm4) — all reach `Dedicated map ready` with no errors.
- Client listen server in Clan Arena on q4dm1: player spawns, world renders,
  no errors and no unresolved `#str_` ids.
- Two-player round progression has not been exercised; openQ4 has no bot
  support in the game module, so it needs the two-instance harness.
- The second pass above is compile-verified only. Every change is server-side
  game logic or gui state, and none of it has been played.

## Follow-up status

- The former client round-reset issue now has an append-only
  `GAME_RELIABLE_MESSAGE_ROUNDRESTART` message that calls `LocalMapRestart`
  without recreating the game state. Runtime qualification is tracked in the
  [competitive reference audit](../competitive-match-reference-audit.md).
- Managed Red Rover uses a dedicated authoritative conversion path. Ordinary
  live joins and voluntary side changes still use the normal admission policy.
  A conversion may fill one side, while locked roster seats and captain roles
  retain their declared identity; round preparation restores those home sides.
  The production core covers full-server and locked-roster conversions, and
  two real clients completed the eight-round profile with seven remote resets.
- The six best-of profile strings `#str_41693`–`#str_41698` are now present
  in the language tables. The role, controller and display qualification
  matrix remains tracked in the competitive reference audit.
