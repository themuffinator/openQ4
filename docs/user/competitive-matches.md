# Competitive Matches

> **Experimental.** Multiplayer and the single-player Arena Campaign are
> under active development and are not yet considered stable. Expect rough
> edges, changing behaviour between builds, and bugs. They are not
> representative of the finished feature.

## Current status

openQ4's managed competitive-match framework is implemented in the current
working tree. The server owns one match session for rules, readiness, roles,
rosters, pauses, proposals, series progress, recipient-specific views and match
evidence. The localized **Match Control** page exposes those same typed actions
to players, captains, coaches, broadcasters, referees and a local listen-server
operator.

Automated domain, protocol, storage, localization and UI-contract tests cover
the implementation, including the transactional series/report boundary and
the casual/managed compatibility boundary. Automated gameplay runs also cover
supported modes, role permissions, repeated pauses, BO1/BO3/BO5 series,
multi-map recovery and playback of generated MVDs. Remaining qualification
includes roster and invitation lifecycles, proposal and map-load failure paths,
and complete windowed keyboard/controller workflows. Until that matrix is
signed off, a league should
retain its normal independent result-verification procedure rather than
treating one openQ4 artifact as its sole authority.

The detailed implementation and remaining acceptance work are tracked in the
[competitive reference audit](../dev/competitive-match-reference-audit.md).
Persistent results and spectator controls pass windowed OpenGL and Vulkan
gameplay checks, including server camera restrictions, winner disconnects,
late joins and clearing the previous result at the next countdown. Script
includes also pass map-cycle loading and Windows/Linux native regressions.

## Casual servers and managed matches

A normal server stays casual unless its operator selects a managed competitive
profile. The `casual` profile preserves the familiar flexible server settings,
team menu and legacy vote behaviour. A managed profile instead enables:

- a server-authoritative ready gate with explicit blockers;
- a validated rules snapshot which is frozen for the map;
- connection-scoped players, captains, coaches, broadcasters and referees;
- roster seats, substitutions, team locks and the Duel queue;
- per-team or Duel-contestant timeout budgets and referee technical pauses;
- typed proposals which cannot execute arbitrary console text;
- optional best-of series, map vetoes and atomic cross-map recovery; and
- bounded JSON evidence with automatic server-side MVD recording.

Managed matches reject the inherited text-command vote routes and mutable
legacy server-admin settings, then use the typed proposal and Match Control
services. A vote which was already open—or had passed and was waiting for its
old delayed execution—is cancelled when managed authority becomes active.
Casual matches retain those compatibility paths. Ban-list administration
remains a separate rcon/server-security responsibility and never grants a
match role. Selecting a competitive profile does not grant rcon, expose server
files or require any replacement Quake 4 assets.

The supported competitive modes are:

- DM and Tourney;
- Duel;
- Team DM;
- CTF, One Flag CTF, Arena CTF and Arena One Flag CTF;
- DeadZone; and
- Clan Arena, Freeze Tag and Red Rover.

Overload, Harvester, Domination and Attack & Defend remain reserved wire values.
They do not have the complete authoritative game-state and scoring path needed
for managed competition, so they stay hidden from hosting, voting, browser
filters and map compatibility checks.

## Casual warmup and match endings

With warmup enabled, `si_minPlayers` counts active humans and bots together;
at least one active human is required. Bots do not vote in the casual ready
percentage. Duel uses its two seated contenders and requires every human
contender to be ready; waiting spectators do not affect the gate. Practice
with warmup disabled retains the one-human start policy.

Casual team games need opponents on both sides. `si_teamForcePresent 1`
requires both teams to meet `si_teamSizeMin`; with it disabled, one side must
meet that larger minimum. Red Rover keeps its own round roster handling.

Players and captains may withdraw readiness during countdown. If the ready
requirement no longer holds, the server cancels that countdown and starts a
fresh one once the requirements are met again.

Ties at a frag or capture limit enter sudden death after the finishing delay.
Timed overtime is granted when the match clock expires with a tie. The
current score leader wins when both teams have passed the score limit.
Losing the required opponents ends live play even with no time limit;
losing the last active human aborts the match while any remaining bots wait
for players. Completed and cancelled matches clear their pending timers.

Set `si_useReady 0` to start once the population requirement is met, or
`si_warmup 0` for warmup-free practice. A zero `si_warmupReadyPercentage`
also disables the percentage gate outside Duel; Duel still requires both
contenders when ready-up is enabled.

Casual option voting is enabled with `si_allowVoting 1`. Only active human
players enter a vote's electorate; AI bots cannot call or cast votes and do
not raise the majority needed to pass one. The electorate stays fixed until
that vote ends, including when players disconnect.

Clan Arena, Freeze Tag and Red Rover reset the world between rounds while
remote clients retain the ongoing match state. Use matching updated clients
and servers for the new round-reset message.

Managed Red Rover converts a defeated player to the opposite gameplay side
without opening ordinary live team changes. Declared roster seats and captain
roles remain intact, and the next round restores the roster's home sides.
Team text and voice routing follow the player's current gameplay side.

In Freeze Tag, frozen bodies remain available after overkills. Stand within
`si_freezeThawRadius` with a clear view of a teammate for
`si_freezeThawTime` seconds to rescue them and earn a point. Stepping away
reduces the accumulated rescue progress. `si_freezeThawThroughSurface 1`
explicitly permits rescues through geometry.

DeadZone awards control time only during live play. Dead or spectating players
and players without a required token cannot control or contest the zone;
opponents who remain eligible contest it regardless of entry order.

One Flag modes can use the original CTF maps. When a map has both team bases
but no neutral flag, openQ4 places the stock neutral flag at the unteamed
pickup location nearest both bases. Map-authored neutral flags take priority,
and explicit One Flag entity filters override the inherited CTF layout.
For organized competition, review the chosen map's objective placement as
part of selecting the map pool. Update both clients and servers together;
older development CTF demos require their recording build.

Tied scores share their place and occupy every tied seat: `10, 10, 9` means
first, first, third. Duel ranks only its two contenders; waiting players stay
in the spectator list. Both finalists remain on the final scoreboard until
review ends and the next challenger takes the loser's seat.

Managed Duel uses the same winner-stays flow. The waiting queue preserves
arrival order: joining it again does not move a player forward, and deferring
moves them behind the current challengers. After review, the loser joins the
back of the queue and the next challenger takes the vacant seat. A forfeit
retires the forfeiting player even if they were ahead on score. A tied result
returns both finalists to the queue. Promoted players must ready up for the
next match. An unfinished best-of series keeps its two contestants between
maps. Aborting a match preserves both contestants and the waiting queue.

Managed summaries show the committed outcome separately from the score table:
the winning player or team, a forfeit, a draw, or an aborted match. A forfeit
awards the validated opponent even when the forfeiting contestant led on score.
Summary rows retain their original scores and score order; draws and aborts
do not label either side a winner.

The result freezes the winning participant's identity and display name, or the
winning team, when the match ends. Later renames, disconnects, slot reuse and
team changes cannot rewrite it. It remains visible through review and the
following warmup, and clears when the next countdown starts or a new session
begins. **Match Control > Status** shows it as **Last Result**, including during
the following warmup. This public result is available even when optional
match-evidence recording is disabled.

The rules snapshot now distinguishes **Minimum Active Players** (including
bots) from **Minimum Active Humans**. Use matching updated clients and server
binaries, and finish any active managed series before upgrading: rules
identities from the previous snapshot schema do not match the new schema.
Duel contender membership also uses an updated network layout. Structured
match views now use schema 4 to carry the committed result; clients and servers
must use the same updated build. Replay older development multiplayer demos
with the build that recorded them.

## Start a managed server

Follow the regular [server setup guide](server-setup.md), then select an
applicable profile before the session starts. This example uses stock Quake 4
assets:

```text
openQ4-ded_x64 +set si_name "Managed Duel" +set si_map mp/q4dm1 +set si_gameType Duel +set g_matchProfile competitive_duel +spawnServer
```

The built-in profile keys are:

| Profile | Intended modes | Main policy |
| --- | --- | --- |
| `casual` | Every implemented public mode | Flexible server settings; managed-match restrictions are off |
| `competitive_dm` | DM | Unanimous human readiness, no frag limit |
| `competitive_tourney` | Tourney | Unanimous human readiness for the tournament arena |
| `competitive_duel` | Duel | Two active humans, unanimous readiness, two 60-second timeouts per contestant |
| `competitive_tdm` | Team DM | Unanimous human readiness, friendly fire, 15 minutes, 50 frags, two 60-second timeouts per side |
| `competitive_ctf` | CTF family | Unanimous human readiness, friendly fire, 20 minutes, five captures, two 60-second timeouts per side |
| `competitive_deadzone` | DeadZone | Unanimous human readiness, friendly fire, 15 minutes and 120 seconds of control |
| `competitive_round` | Clan Arena, Freeze Tag and Red Rover | Unanimous human readiness, eight rounds, three-minute round limit, sudden death |

Profiles are mode checked. An inapplicable profile is rejected in favour of the
mode's recommended profile. The read-only `si_matchRules` server-info value
publishes the committed profile, schema and digest for diagnostics; the digest
is not a password or proof of server identity.

Initial auto-join waits for the managed warmup session to become available.
An authorized force-ready action then starts the normal countdown and enters
gameplay when that countdown expires.

For a series, `si_mapCycle` may contain a semicolon-delimited pool such as:

```text
set si_mapCycle "mp/q4dm1;mp/q4dm3;mp/q4dm5;mp/q4dm7;mp/q4dm9"
```

Every entry is normalized, deduplicated, checked as an installed map and
validated for the current mode. If the value is empty, the server builds a
bounded deterministic pool from compatible installed maps.

Map-cycle scripts placed at the game-directory root now resolve relative
includes correctly on Windows and POSIX systems. No replacement scripts or
assets are required for this path-handling fix.

If remote referees are needed, set `g_refPassword` through a protected server
configuration or trusted local server console before the match session begins.
The game converts it into a verifier and clears the cvar at session start. It is
not rcon and should not be reused as an administration password.

## Use Match Control

During multiplayer, open the in-game menu and choose **Match Control**. Its
**Status**, **Teams**, **Proposals**, **Rules**, **Series** and **Evidence** tabs
are projections of the latest view authorized for your current connection.

Unavailable actions remain visible and show the server's localized reason.
Mouse hover and keyboard/controller focus both reveal that explanation.
Long explanations and operation results wrap and can be scrolled. Readiness
is shown during warmup and countdown; live play and review do not label
players unready, and observers are not asked to ready up.
Selections refer to bounded structured rows, not text parsed from a player name
or translated label. Destructive actions such as force-ready, committing rules,
removing or substituting a roster member, cancelling a series, forfeiting and
aborting require confirmation. Match-changing requests carry the accepted
session and control revision, and the server rejects stale state before
changing anything.

The **Status** tab also shows the committed **Last Result** and, while you are
spectating, a camera row with **Previous**, **Next** and **Free Camera** buttons.
These use the same live camera requests as the console commands below; the
server checks your current permissions when each request arrives.

The compatibility commands below still submit the managed ready operation:

```text
ready
notready
readyup
```

They do not form a second ready system. A spectator, bot or disconnected player
cannot satisfy a managed human ready gate, and a roster or rules change can send
an invalid countdown safely back to warmup.

## Role workflows

Roles are capability bundles, not an administration hierarchy. All role and
ready state belongs to the current connection and session. A reused client slot,
display name, IP address or userinfo value never recovers authority.

### Player

Use **Status** and **Teams** to:

- join a legal side, spectate, or join/defer/leave the Duel queue;
- accept a roster invitation and see playing, waiting, bench and ready state;
- set ready or not ready;
- request and resume your own contestant timeout in managed Duel;
- create an eligible proposal and vote yes, no or abstain; and
- forfeit only the contestant or side represented by the current connection.

The Duel queue is FIFO and keyed by the session participant, not a name. A
promoted contestant must still ready. Disconnecting removes connection-scoped
queue, ballot and ready authority.

Once admitted to a managed Duel, use **Request timeout** in **Status** to spend
your contestant's budget, shown as **Contestant A** or **Contestant B**. Use
**Resume** to end your own pause; gameplay restarts after the resume countdown.
This does not grant captain or team-management permissions.

### Captain

For a managed team match, the operator first sets **Roster Size Per Team** in
**Rules**. The first declared seat on each side is its captain seat. Select the
participant in **Teams**, choose **Captain**, and use **Assign role**. Consecutive
administrative changes have a short cooldown; wait for the action to become
available again before the next change.

A captain receives the normal player actions plus the permitted side-scoped
controls:

- set team readiness and lock or unlock that side;
- invite, remove, assign roles and atomically substitute roster members;
- keep substitutes on a durable bench without counting them as active players;
- request the side's tactical timeout and consent to resume; and
- make the current contestant's map-veto or starting-side choice.

Captain authority is limited to the current side and session. It is never rcon,
ban-list or filesystem access.

Invitations require the issuer's current authority when they are accepted.
Disconnecting, demoting or benching a captain revokes that captain's outstanding
invitations. Accept invitations and assign roster roles during warmup;
substitutions also remain available during countdown. Review and next-game
screens explain when these actions are unavailable. Self-withdrawal for an
inactive coach or substitute remains available independently.

Captain-controlled invitations for an ordinary neutral spectator to bypass a
managed live POV lock are deliberately disabled in this release. They fail
closed rather than reusing a roster invitation as a spectator credential. Give
an observer an explicit coach or broadcaster role through an authorized path
instead.

### Coach

An authorized roster/role action assigns a coach to one side. A coach is an
inactive observer, can receive that side's permitted vitals and can follow only
that side during protected live play. The role does not imply readiness, roster,
pause, rules or veto authority, and it cannot be combined with an active player,
broadcaster or referee role.

An inactive coach or substitute can use **Leave roster** on the **Teams** page
to withdraw their own seat. This narrowly scoped action never grants general
team-changing or captain authority and cannot remove another participant.

### Broadcaster

A broadcaster is an inactive, neutral, unrostered human observer. The role may
receive both sides' permitted live vitals and follow targets but cannot mutate
the match. In managed matches, explicitly granted broadcasters and referees may
also receive authoritative timers for supported placed, respawning major items.
Those timers use the pause-safe match clock and are never projected to players,
coaches or ordinary spectators. The UI renders localized item names (or a
generic localized fallback), never an internal item token.

openQ4 does not currently provide a functioning Q4TV/repeater transport or a
live repeater recipient. The match-view policy contains a public-only,
no-item-timing result for a possible future repeater adapter; that fail-closed
boundary is not a shipped live capability and never inherits broadcaster or
referee authority. Delayed broadcast and live multi-POV composition remain
deferred. The supported recording path is the server-owned MVD described below.

On a listen server, the local operator can select an eligible participant on
**Teams** and choose **Grant broadcaster** or **Revoke broadcaster**. A dedicated
server operator can use the equivalent trusted-local command:

```text
matchBroadcaster <current-client-slot> <on|off>
```

For example, `matchBroadcaster 5 on` resolves slot 5 against its current
connection binding and grants only the broadcaster role. The command rejects an
active player, bot, roster member, sided participant, referee, coach, stale slot
or any other ambiguous target.

### Referee

An eligible inactive, neutral, unrostered human enters the configured credential
on **Status** and chooses **Sign in**. Authentication uses a fresh challenge and
proof, rate limiting and constant-time verification; the reusable password is
not sent as a console command, replicated in a match view or written to evidence.
The UI clears its local credential buffer after success or failure.

The referee can then use the actions authorized for the current phase to:

- force the readiness gate with an audited override;
- apply a technical pause and resume countdown;
- stage, validate, commit or discard rules at a legal boundary;
- manage exceptional roster and role changes;
- moderate proposals;
- stage, start, operate, advance or cancel a series; and
- record an abort or forfeit.

Choose **Sign out** when finished. Referee access is connection scoped, is
revoked on disconnect or session change, and never grants console, rcon,
ban-list, process or filesystem authority.

### Server operator

The server operator chooses the profile, referee credential and series map
pool, monitors the console and evidence outputs, and uses only trusted-local
adapters for operations which cannot safely belong to a remote role. A local
listen host receives operator-only Match Control availability. Dedicated
servers also provide `forceReady`, `matchBroadcaster` and the Duel recovery
command described below; these adapters build the same typed requests and use
the same schema, phase, capability, target and revision checks as Match Control.
The inherited server-settings, shuffle, kick and force-team administration
paths fail closed while a managed session is active. Configure managed rules,
rosters and participant removal through Match Control instead. Ordinary
ban/unban remains outside match authority and requires the normal trusted
server administration channel.

## Pauses and timeouts

A tactical timeout belongs to one side and consumes one unit of that side's
budget when the pause commits. Duplicate or late requests cannot consume it
twice. A referee technical pause is a separate exceptional action and does not
charge either team.

While paused:

- network traffic, chat, view movement, scoreboards and Match Control continue;
- player movement, attacks, damage and gameplay simulation remain frozen; and
- resume uses a server-owned countdown before match time advances again.

The engine/network clock remains monotonic. Regulation, overtime, round,
respawn, projectile, mover, item, powerup, weapon and gameplay-event deadlines
are frozen or rebased exactly once through the centralized pause adapter.
Automated gameplay checks cover repeated world pauses and Clan Arena/Tourney
countdown pauses; remaining mode and overlapping-request cases are tracked in
the competitive reference audit.

## Proposals

Proposals contain one validated match operation and typed arguments, never a
console command string. The human electorate and required yes count are frozen
when a proposal opens, so a join or disconnect cannot move the threshold.
Bots and ordinary spectators do not silently decide competitive settings.

One global proposal and one proposal for each side can coexist. Match Control
shows the caller, action, remaining time, vote counts, threshold, eligibility
and recorded ballot. The current creation templates are global and target-free;
the side selector can address an already-authorized side proposal for voting or
cancellation but does not invent a side or participant target. Adding a targeted
template requires an explicit protocol and authorization review.

When a proposal fails, expires, is canceled or becomes invalid after a phase
change, its slot is released for another proposal. The normal cooldown still
applies, and an old ballot cannot affect the replacement proposal.

Legacy vote commands remain available on casual servers. A managed session
rejects those old routes so they cannot bypass the proposal validator.

## Series, veto and cross-map recovery

Match Control supports best-of-one, best-of-three and best-of-five series for
Duel and team modes. The server validates the profile, two contestants, current
mode, installed map pool and deterministic veto pattern as one transaction.
Each ban, pick, decider or starting-side choice is turn- and revision-checked.

Completing the veto schedules the first selected map automatically. After each
map's review, **Advance map** loads the next validated selection or completes
the series when a contestant has enough wins. The server records the chosen
side mapping, starts the map through the normal `nextMap` path, and commits only an authoritative decided or
forfeit result to the series score. An abort does not award a series point. If a
selected map's metadata or actual map file is unavailable before handoff, the
attempt is recorded and the series remains ready to retry. This preflight does
not recover corrupt map data or failures occurring after loading begins.

Every active series mutation is captured in one schema-versioned bounded
checkpoint under the active save path:

```text
baseoq4/match-series/series-<series-id>.oq4series
```

Current schema-4 checkpoints store the series core and its mutable report draft
together. One checksummed promotion is therefore the only durable commit point: a map cannot
advance without the corresponding report row, and a report row cannot get
ahead of the series score. Paired schema-3 checkpoints remain supported.
Schema-2 recovery files can still be decoded for diagnosis, but are not resumed
because they have no paired report state.

The write uses a same-directory pending file and atomic promotion. The archived
server cvar `g_matchSeriesRecoveryId` is updated automatically while a series is
active and cleared when it completes or is cancelled. On the next map or server
start, openQ4 validates that identity, record digest, rules digest, mode, state,
current map and every installed pool entry before transactionally restoring
both aggregates. The selected-map checkpoint is distinct from the newly bound
map session, so a lobby or previous-map journal cannot be mistaken for the next
map's result. Do not edit the record or cvar during an ordinary series. If an
operator intentionally abandons an unrecoverable series, set
`g_matchSeriesRecoveryId 0` and retain the old file for diagnosis.

If the restored checkpoint was saved after a completed map, Match Control
shows the saved review in warmup. Use **Advance map** to continue to the next
selection or publish a completed series. The saved score and reports remain
attached to that series.

Team series authority follows the recovered competition-side to current
Marine/Strogg-side mapping. Duel has no persistent gameplay side and openQ4 does
not store a name, address, client slot or other counterfeit identity in the
recovery file. After a Duel recovery, a trusted server operator must bind both
current active human connections during warmup:

```text
matchSeriesBind a <current-client-slot>
matchSeriesBind b <current-client-slot>
```

For example:

```text
matchSeriesBind a 3
matchSeriesBind b 7
```

Use the server's current client listing, not slots copied from an earlier map or
connection. Each binding dies with that connection and is never persisted. The
command fails unless the server is in Duel warmup with a live recovered series
and the target is a current active human contestant.

## Spectator privacy

The server builds a separate bounded view for every recipient and authorizes
data before serialization. Match Control cannot reveal information merely by
hiding a row after the client received it.

During managed countdown and live play, neutral ordinary spectators are locked
out of both live POV domains. Coaches can follow their own side; explicitly
granted broadcasters and referees use their role-specific paths. Every actual
camera cycle rechecks the current session, connection generation, role, target
and lock state on the server. Stale rows are discovery data, not reusable
permission tokens.

Human spectators can use the **Status** camera row or these console commands
for live camera navigation:

| Command | Action |
| --- | --- |
| `follow next` or `follownext` | Follow the next eligible player. |
| `follow prev` or `followprev` | Follow the previous eligible player. |
| `follow free` or `followfree` | Request free flight under the current camera policy. |
| `follow <client-slot>` | Request the player currently occupying that numeric client slot. |

Every request obeys the server's current spectator permissions and locks.
Direct requests also validate the target's current spawn identity, so a stale
request cannot follow a replacement player who reused the slot. Existing
eligible bot targets remain available; bots cannot issue these observer
commands. Demo playback keeps its separate camera controls.

The live policy exposes authorized team vitals and follow targets. For managed
matches, it also tracks supported placed, respawning major items by stable
map-instance and source identity. Pickup and respawn deadlines use match time,
so a pause neither consumes a timer nor causes a jump on resume. Item timers are
projected only to explicit broadcaster and referee recipients; players,
coaches, ordinary spectators and the fail-closed future repeater policy receive
none. Presentation maps supported semantic identifiers to localized names and
never renders a raw internal token.

Captain spectator invitations remain disabled, as described above. The engine
has no reachable Q4TV/repeater transport in this release. Its pure public-only
projection rule is reserved for a future adapter, not advertised as live
repeater support. Delayed broadcast and live multi-POV composition are separate
future work; an unrestricted spectator view is never described as a delay
service.

## Match evidence and MVDs

A managed match keeps a bounded evidence journal and final statistics.
`g_matchEvidence` controls the optional per-map artifact and defaults to `2`:

- `0` keeps no per-map JSON artifact (an active series still keeps the minimum
  in-memory result journal needed for its transactional report);
- `1` writes the final per-map JSON; and
- `2` writes the full bounded journal and final per-map JSON.

At finalization the server stops an automatically owned MVD, records any output
failure, then attempts to atomically promote the per-map JSON beneath the
active save path:

```text
baseoq4/match-results/session-<session>_series-<series>_<map>.json
```

The schema-versioned report includes server-authored build, map, mode, rules
digest, series link, bounded lifecycle/pause/roster/role/proposal events, final
participant and team statistics, map result, journal drop counters and the safe
qpath of a linked MVD. It excludes IP addresses, passwords, authentication
material and transient series connection bindings. Output failure never changes
the winner.

For a series, each authoritative map result, score, rules identity, session and
evidence/MVD artifact status is appended to the paired report draft before the
series and report are checkpointed together. On completion or cancellation,
openQ4 first atomically stores the immutable schema-1 series report and only
then publishes the terminal recovery checkpoint:

```text
baseoq4/match-results/series-<series-id>.json
```

That report contains the stable series/profile/rules identity, report-scoped
contestants, ordered map attempts and outcomes, available aggregate statistics,
artifact availability/failure reasons, drop counters and final authorizer. It
does not use a client slot, address or display name as reconnect authority.

A managed profile starts server-side MVD recording after the rules freeze for
countdown and stops it after result metadata is committed to the journal. If an
operator already started a server MVD, the match does not take ownership or stop
it. A recording still open at map completion has no playable link in that map's
sealed JSON. Stopping it before the series is finalized lets the series report
link the completed file; if it remains open when the series is finalized, that
immutable report retains its pending status even after a later manual stop.
Use Match Control's **Evidence** tab to distinguish
initialized, recording, finalized and persisted state. A displayed qpath is not
by itself proof that promotion succeeded.

Completed `.mvd` files appear in the main-menu **Demos** library and can be
validated and played with the controls in
[Demo Playback and Multi-View Demo Architecture](../dev/multiview-demos.md#current-operator-interface).
A `.mvd.part` is a recoverable incomplete stream, not a completed match artifact.
If either the JSON or MVD fails, preserve the log, pending file and other output
for diagnosis rather than renaming a partial file as evidence.

## Qualification checklist

For a release or league signoff:

1. Run the correct mode in a window and complete the full player, captain,
   coach, broadcaster, referee and operator workflow through Match Control.
2. Verify every unavailable action shows the expected localized reason and that
   stale menus cannot commit an operation.
3. Exercise individual/team readiness, countdown cancellation, tactical and
   technical pauses, repeated resume cycles, overtime and round transitions.
4. Complete BO1, BO3 and BO5 veto/result paths, including one map-load failure,
   team recovery and explicit Duel rebinding.
5. Inspect recipient bytes and live camera cycling for ordinary spectators,
   coaches, broadcasters and referees; forbidden enemy data must be absent
   rather than cosmetically hidden. Exercise supported placed major-item pickup,
   respawn and pause cycles, proving that only broadcaster/referee views contain
   timers. Test the synthetic future-repeater policy separately for its
   public-only, no-timer result; there is no live repeater transport to exercise.
6. Verify the JSON report was atomically persisted, its series and MVD links are
   correct, and the generated MVD plays through the Demos library.
7. Validate the Match Control layout and keyboard/controller navigation at 16:9
   and 4:3 without taking over user input or using fullscreen.

The HUD and scoreboard now share a compact recipient-scoped card for role,
phase, pause, readiness, timeout budgets, proposals, series progress and any
authorized item timing. Match Control remains the detailed surface for actions,
denial reasons, roster management, rules and evidence.
