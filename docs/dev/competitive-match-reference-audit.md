# Competitive match reference audit

Date: 2026-08-03  
Qualification review: 2026-09-07

Status: Implemented; scoped live qualification recorded, remaining release checks below

This document records the behavioural comparison behind openQ4's competitive
match framework and the remaining gap to release qualification. The
authoritative design remains the
[competitive match framework specification](plans/2026-08-03-competitive-match-framework.md).
This audit does not turn reference-mod behaviour into a compatibility promise.

## Reading the status column

- **Integrated** means the behaviour is connected to the live multiplayer,
  UI or persistence adapter in the current working tree. It still needs any
  qualification named in the final column.
- **Qualification pending** means the implementation and automated contracts
  exist, but the named interactive, cross-role or gameplay matrix has not yet
  been signed off.
- **Scoped live checks pass** means the named packaged-runtime scenarios have
  passing reports. This does not establish untested workflow edges, physical
  input navigation or coverage beyond the recorded runtime snapshots.
- **Remaining** means the concern is an accepted release closure item and is
  not described as delivered.
- **Deferred** is an explicit product boundary, not an accidental omission.
- **Rejected** means the precedent informed the design but is intentionally not
  part of openQ4.

The source status below is based on the local `OpenQ4` and `openQ4-game`
working trees, with qualification reconciled on the review date above. Earlier
dated sections retain the investigation history; the current evidence ledger
and release closure list at the end identify which findings are superseded.
Automated contracts are not substituted for live gameplay or physical-input
evidence. Engine menu diagnostics establish their named action paths without
claiming keyboard/controller navigation.

## openQ4 implementation source map

The main auditable boundaries are deliberately small:

- lifecycle, participants, roles, readiness and pause:
  [`MatchSession`](https://github.com/themuffinator/openQ4-game/blob/main/src/mpgame/mp/match/MatchSession.cpp);
- typed rule descriptors, transactions and built-in profiles:
  [`MatchRules`](https://github.com/themuffinator/openQ4-game/blob/main/src/mpgame/mp/match/MatchRules.cpp);
- bounded wire requests/results and operation descriptors:
  [`MatchProtocol`](https://github.com/themuffinator/openQ4-game/blob/main/src/mpgame/mp/match/MatchProtocol.cpp)
  and
  [`MatchOperations`](https://github.com/themuffinator/openQ4-game/blob/main/src/mpgame/mp/match/MatchOperations.cpp);
- challenge/proof referee authentication and throttling:
  [`MatchAuthentication`](https://github.com/themuffinator/openQ4-game/blob/main/src/mpgame/mp/match/MatchAuthentication.cpp);
- join evaluation, locks, queue, invitations and transaction plans:
  [`MatchTeams`](https://github.com/themuffinator/openQ4-game/blob/main/src/mpgame/mp/match/MatchTeams.cpp);
- best-of state and deterministic veto:
  [`MatchSeries`](https://github.com/themuffinator/openQ4-game/blob/main/src/mpgame/mp/match/MatchSeries.cpp);
- schema-versioned atomic cross-map state:
  [`MatchSeriesRecovery`](https://github.com/themuffinator/openQ4-game/blob/main/src/mpgame/mp/match/MatchSeriesRecovery.cpp)
  and
  [`MatchSeriesRecoveryFileSystem`](https://github.com/themuffinator/openQ4-game/blob/main/src/mpgame/mp/match/MatchSeriesRecoveryFileSystem.cpp);
- bounded series-result state and immutable atomic JSON:
  [`MatchSeriesReport`](https://github.com/themuffinator/openQ4-game/blob/main/src/mpgame/mp/match/MatchSeriesReport.cpp),
  [`MatchSeriesReportStorage`](https://github.com/themuffinator/openQ4-game/blob/main/src/mpgame/mp/match/MatchSeriesReportStorage.cpp)
  and
  [`MatchSeriesReportFileSystem`](https://github.com/themuffinator/openQ4-game/blob/main/src/mpgame/mp/match/MatchSeriesReportFileSystem.cpp);
- recipient authorization, tactical disclosure and serialization:
  [`MatchView`](https://github.com/themuffinator/openQ4-game/blob/main/src/mpgame/mp/match/MatchView.cpp)
  and
  [`MatchDisclosurePolicy`](https://github.com/themuffinator/openQ4-game/blob/main/src/mpgame/mp/match/MatchDisclosurePolicy.cpp);
- authoritative placed-major-item registry and recipient filtering:
  [`MatchItemTiming`](https://github.com/themuffinator/openQ4-game/blob/main/src/mpgame/mp/match/MatchItemTiming.cpp);
- audit journal and atomic JSON boundary:
  [`MatchEvidence`](https://github.com/themuffinator/openQ4-game/blob/main/src/mpgame/mp/match/MatchEvidence.cpp)
  and
  [`MatchEvidenceStorage`](https://github.com/themuffinator/openQ4-game/blob/main/src/mpgame/mp/match/MatchEvidenceStorage.cpp);
- bounded evidence projection:
  [`MatchEvidenceView`](https://github.com/themuffinator/openQ4-game/blob/main/src/mpgame/mp/match/MatchEvidenceView.cpp);
- presentation-only Match Control projection and row model:
  [`MatchControlModel`](https://github.com/themuffinator/openQ4-game/blob/main/src/mpgame/mp/match/MatchControlModel.cpp)
  and
  [`matchcontrol.gui`](https://github.com/themuffinator/OpenQ4/blob/main/content/baseoq4/pak0/guis/matchcontrol.gui);
- live game adapters:
  [`MultiplayerGame`](https://github.com/themuffinator/openQ4-game/blob/main/src/mpgame/MultiplayerGame.cpp); and
- engine-owned recording service:
  [`MultiViewDemo`](https://github.com/themuffinator/OpenQ4/blob/main/src/framework/async/MultiViewDemo.cpp)
  and
  [`NetworkSystem`](https://github.com/themuffinator/OpenQ4/blob/main/src/framework/async/NetworkSystem.cpp).

The local working trees may be ahead of those branch links while this feature
is being assembled; the filenames and boundaries are the audit authority for
the status matrix.

## Reference conclusions

The references agree on the human workflows more than on implementation:
players ready, captains manage their side, referees resolve exceptional states,
timeouts suspend play without consuming match time, spectators receive an
appropriate view, and completed matches leave useful evidence. Their internal
designs vary considerably.

- **Q4MAX** is the closest Quake 4 usability reference. Its documented command
  surface includes timeout/time-in, team-ready, player and spectator locks,
  spectator invitations, multipov and scoped referee access. These are useful
  workflow expectations, not APIs to reproduce. See the preserved
  [Q4MAX command summary](https://quake4.net/console-commands/) and
  [server setup notes](https://quake4.net/server-setup/).
- **CPMA** demonstrates that captain and coach workflows can operate a match
  without granting full server control. Its ready, timeout, captain, coach,
  invite, lock and referee commands are documented in the
  [CPMA command guide](https://www.playmorepromode.com/guides/cpma-commands/).
  Its mode system also shows why coherent per-gametype profiles are preferable
  to one enormous global configuration, while warning that weakly validated
  config composition is easy to misuse. See
  [CPMA server settings](https://cpma-news.org/guides/content/config/serversettings)
  and [custom modes](https://cpma-news.org/guides/content/config/custommodes).
- **OpenTDM** supplies a clear warmup/countdown/play/overtime/sudden-death/
  scoreboard lifecycle, captain-controlled team readiness and locks, timeouts,
  and privacy-aware live statistics. The local archive's principal evidence is
  `g_local.h`, `g_tdm_core.c`, `g_tdm_cmds.c` and `g_tdm_stats.c`; the upstream
  project and source location are documented at
  [opentdm.net](https://www.opentdm.net/) and
  [notr1ch/opentdm](https://github.com/notr1ch/opentdm).
- **WORR** demonstrates a modern session hub, a role-sensitive tournament veto,
  confirmations for destructive actions, an artifact catalog, and task-based
  documentation. The audited snapshot is commit
  [`46b0387`](https://github.com/DarkMatter-Productions/WORR/tree/46b03878154a7e9f6f384ef6468aee8ae2d8e068).
  Relevant sources include its
  [competitive server guide](https://github.com/DarkMatter-Productions/WORR/blob/46b03878154a7e9f6f384ef6468aee8ae2d8e068/docs-user/competitive-server-tools.md),
  [session-menu guide](https://github.com/DarkMatter-Productions/WORR/blob/46b03878154a7e9f6f384ef6468aee8ae2d8e068/docs-user/multiplayer-session-menu.md),
  [tournament core](https://github.com/DarkMatter-Productions/WORR/blob/46b03878154a7e9f6f384ef6468aee8ae2d8e068/src/game/sgame/match/tournament.cpp)
  and
  [match logging](https://github.com/DarkMatter-Productions/WORR/blob/46b03878154a7e9f6f384ef6468aee8ae2d8e068/src/game/sgame/match/match_logging.cpp).
- **AfterShock XE** is valuable both as a feature inventory and as negative
  design evidence. It includes warmup readiness, timeouts, rich votes,
  multiview and statistics, but its local `g_vote.c` ultimately schedules
  textual console commands and its match policy is spread across cvars and
  unrelated subsystems. openQ4 keeps the behaviours that help players and
  rejects that authority shape. Sources:
  [project overview](https://github.com/Irbyz/aftershock-xe),
  [vote implementation](https://github.com/Irbyz/aftershock-xe/blob/master/code/game/g_vote.c),
  [match loop](https://github.com/Irbyz/aftershock-xe/blob/master/code/game/g_main.c)
  and
  [change history](https://github.com/Irbyz/aftershock-xe/blob/master/CHANGELOG-AfterShock-XE.md).
- **Quake 4 itself** already provides the right engine-shaped foundation for a
  server recording and player-follow workflow. openQ4 extends that foundation
  instead of pretending a client recording is an authoritative match artifact;
  see the archived
  [idDevNet network-demo documentation](https://iddevnet.dhewm3.org/quake4/NetworkDemos.html)
  and openQ4's [MVD architecture](multiview-demos.md).

Q4MAX multipov is a behavioural reference, not a statement of current openQ4
transport support. openQ4 ships the connection-scoped broadcaster role and
server-owned MVD recording. The engine does not implement a reachable live
Q4TV/repeater transport; the match-view core's public-only repeater outcome is a
fail-closed boundary for a possible future adapter. Delayed broadcast and live
multi-POV composition remain deferred.

## Gap and decision matrix

| Concern | Reference lesson | openQ4 decision | Current status | Release gap / acceptance |
| --- | --- | --- | --- | --- |
| Match lifecycle | OpenTDM makes administrative and live phases explicit; Q4MAX exposes referee abort/ready control | Preserve Quake 4 wire phases, enforce one legal transition table and record a reason | **Integrated; scoped live checks pass** for Duel turnover, Tourney brackets, flag captures, DeadZone and the supported round modes | Extend the recorded scenarios to the remaining parent/round edges, readiness invalidation and disconnect transitions; do not treat mode admission alone as complete lifecycle coverage |
| Competition clock | AfterShock's history shows how many world systems can accidentally advance in a timeout | Keep network time monotonic, freeze match time, and rebase each gameplay deadline exactly once | **Integrated; scoped live checks pass** for repeated world pauses and Clan Arena/Tourney countdowns | Retain the projectile, mover, item, powerup, weapon and animation evidence below; qualify remaining supported round adapters and overlapping or duplicate pause requests |
| Rules profiles | CPMA modes give each mode coherent settings, but permit minimally validated config composition | Typed descriptors, cross-field validation, atomic commit, stable digest and frozen map snapshot | **Integrated**, including Match Control staging/commit and legacy mirrors | Validate every profile/mode/map combination in live maps and stale-menu rejection at each rules boundary |
| Readiness | Q4MAX/CPMA/OpenTDM support player and captain readiness | Individual, team, combined or disabled policy with explicit blockers | **Integrated**, including player, captain and audited force-ready entry points | Qualify blocker presentation, countdown cancellation and disconnect/roster invalidation in every supported mode |
| Authority | CPMA distinguishes captain, coach and referee from full server administration | Connection-scoped participant IDs, role-derived capabilities and no implicit numeric hierarchy | **Integrated** with role/seat invariants and recipient-visible availability | Complete the cross-role reconnect and slot-reuse runtime matrix; never restore privilege from name, address or userinfo |
| Referee authentication | Q4MAX offers limited referee access | PBKDF2 challenge/proof, rate limits and an exclusive session-scoped referee grant; never rcon/filesystem power | **Integrated; scoped live checks pass** for sign-in, logout and recipient/camera revocation through menu action handlers | Qualify failure/throttling, disconnect and map/session revocation, plus credential clearing and keyboard/controller navigation |
| Team locks and joins | Q4MAX/CPMA/OpenTDM combine captain locks with invitations | One join evaluator returns allow, queue or deny and one transaction updates team/session state | **Integrated** across typed actions, userinfo reconciliation and spectator transitions | Run full, locked, invited, live, force-team and reconnect cases without a legacy side mutation escaping the evaluator |
| Roster seats and substitutes | Organised team mods distinguish roster membership from the current client slot | Stable session participant/seat model, invitations, role-compatible seats, atomic substitutions and narrow self-withdrawal | **Integrated**, including persistent substitute/bench membership and self-only coach/substitute departure without broad team-join authority | Qualify replacement disconnects, invitation expiry and repeated player/captain/coach/substitute transitions |
| Duel queue | CPMA and WORR keep extra duel players in a predictable waiting flow | Bounded FIFO queue keyed by session participant ID with join/defer/leave/promotion | **Integrated; scoped live checks pass** for duplicate join, winner retention, FIFO promotion, new readiness and a replacement connection preserving the frozen result | Qualify repeated defer, capacity and additional disconnect/promotion orderings in live Duel |
| Tactical timeout | Q4MAX/CPMA/OpenTDM permit a side to stop and resume without general admin rights | Side budget, single owner, frame-boundary commit, configurable resume policy and resume countdown | **Integrated; scoped live checks pass** for timeout/resume and repeated world freeze | Qualify simultaneous/duplicate requests and once-only budget charging; retain the successful repeated-pause scenarios below |
| Technical pause | Referees need an exceptional pause distinct from a charged team timeout | Typed reason, referee/operator capability and no side-budget charge | **Integrated; scoped live checks pass** for Clan Arena/Tourney match and round/arena countdowns, paused aborts and BO3 paused forfeits | Qualify remaining mode/round adapters and conflicting pause requests; the named countdown and terminal scenarios are already recorded |
| Votes and proposals | Reference mods expose useful voting but several ultimately execute console text | A proposal stores one validated typed operation and a frozen human electorate | **Integrated**; managed legacy vote/cast paths reject while casual behaviour remains, and committing managed authority cancels an inherited vote even after it passed into delayed execution | Qualify global/side ballot display, threshold stability across disconnects and proposal revalidation on pass |
| Operation transport | Command-heavy mods duplicate validation at every entry point | One bounded schema, actor binding, optimistic control revision, authorization, cooldown and result | **Integrated** across GUI, compatibility commands, narrow dedicated-console adapters and passed proposals; legacy settings/kick/team/shuffle/restart/next-map adapters fail closed in managed sessions | Retain malformed/trailing/oversized fuzz coverage and verify identical live denial reasons at each entry point |
| Per-recipient state | CPMA coaches and Q4MAX spec locks imply that clients must not receive forbidden information | Authorize before serializing bounded `mpSessionView` v4; GUI never hides secrets it already received | **Integrated; scoped live checks pass** using accepted recipient bytes for coach, spectator, broadcaster, referee and role revocation on GL/Vulkan; the pure repeater result remains a future fail-closed boundary | Extend byte checks to remaining phase/lock/connection-generation transitions; test the synthetic repeater policy separately without claiming a live transport |
| Coach and broadcaster views | CPMA supplies team-only coaching; Q4MAX supplies multipov | Explicit coach/broadcaster/referee audiences and fresh server checks for every follow transition | **Integrated; scoped live checks pass** for 17 direct/cycle/free/revocation camera checkpoints on each renderer | Qualify target disconnect/slot reuse and remaining mode-specific camera edges; captain spectator invitations remain fail-closed and no live Q4TV/repeater path is shipped |
| Match Control UI | WORR demonstrates a stable task-oriented session hub and contextual destructive confirmations | One localized surface driven only by recipient views, descriptors and a bounded structured row model | **Integrated; scoped live checks pass** across six tabs with 800x600 GL and 960x540 Vulkan captures and real menu action handlers | Complete every role's workflow, physical keyboard/controller navigation, credential clearing and remaining layout states |
| HUD and scoreboard | Competitive mods surface timeout, overtime, ready and series context near play | Minimal descriptor/state-key-driven context, not gametype-number branches | **Integrated; scoped live checks pass** for recipient context, persistent terminal results, maximum-length winner names and corrected summary chat clipping at both aspect ratios | Complete remaining mode/role layouts and casual visibility reset |
| Series profiles | WORR demonstrates best-of setup and map order; tournament tools need deterministic state | Optional BO1/BO3/BO5 series with a bounded installed, mode-compatible pool and deterministic veto | **Integrated; scoped live checks pass** for team BO1/BO3/full-distance BO5 and two-human Duel BO3 | Qualify pool bounds, insufficient pools and remaining mode/profile combinations; retain completed profile/admission evidence below |
| Veto and side choice | WORR exposes whose turn it is; competitive practice requires picks/bans and sometimes starting side | Typed BAN/PICK/SIDE steps with expected side and recorded initial side | **Integrated** with full view/history, safe exact-map scheduling and load-failure rollback | Run out-of-turn, duplicate, stale revision, starting-side and failed-load cases through Match Control |
| Cross-map recovery | WORR persists series progress; operators must not reconstruct state from logs | Versioned digest-protected record, fixed qpath, temp write plus atomic replace and no persisted connection identity | **Integrated; scoped live checks pass** for team BO5 interruption/restart, explicit Duel `matchSeriesBind` recovery and failed-MVD status recovery; schema 4 checkpoints series/report draft together and legacy schema 2 is diagnosis-only | Qualify failed map-load rollback and remaining incompatible/stale recovery and replacement-connection edges; successful interruption/restart is already recorded |
| Statistics privacy | OpenTDM hides opponent detail during live play; AfterShock restricts detailed stats until spectating/end | Evidence and recipient views have separate bounded, role-aware responsibilities | **Integrated; scoped live checks pass** for accepted role-filtered vitals/timers and repeated-pause placed-item respawn | Combine live pickup/respawn with remaining recipient/lock/connection transitions; retain byte-level absence checks and keep the synthetic future-repeater case separate |
| Audit/result artifact | WORR shows the value of schema-marked, discoverable artifacts | Bounded per-map schema-v2 journal/final JSON plus an immutable schema-1 series report, escaped serialization, fixed paths and atomic promotion | **Integrated; scoped live checks pass** for linked map/series JSON, interrupted series recovery, blocked publication and Demos discovery | Qualify remaining interrupted-write/capacity boundaries; a blocked final destination does not establish every filesystem failure mode |
| Automatic MVD | Q4MAX/CPMA establish unattended competition recording; Quake 4 has a server-demo base | Managed profiles start openQ4 MVD at frozen countdown, link its qpath, and stop owned recording before report persistence | **Integrated; scoped live checks pass** for automatic/manual ownership, failed start/final promotion, size/duration caps, status recovery, library playback and paused restart-seek cameras; see [dated evidence](mvd-qualification-2026-09-07.md) | Qualify remaining interrupted-write and synchronization failures; retain the separate Red Rover debris-rendering investigation |
| Browser/mode truth | Older two-bit filters and placeholder modes mislead hosts and players | One public set: DM, Tourney, Team DM, CTF, One Flag CTF, Arena CTF, Arena One Flag CTF, DeadZone, Duel, Clan Arena, Freeze Tag and Red Rover | **Integrated registry/contracts** | Cross-check engine mirror, host menu, vote/map compatibility and browser filters at runtime; keep four reserved modes hidden |
| Persistent identity | Some mods approximate reconnect identity with names, GUIDs or addresses | Do not counterfeit identity; roles/readiness/queue votes are connection scoped | **Rejected/deferred** | A future cryptographic account/resume-token service may provide an explicit reattachment API |
| Remote web/brackets | Tournament ecosystems can schedule brackets and administer fleets | Keep brackets, league scheduling and remote administration outside the in-map aggregate | **Deferred** | Future tools consume typed session/evidence APIs; they do not gain game-process authority |
| Forced screenshots/client demos | Historical competition mods can issue client autoactions | Server MVD plus structured evidence; client capture remains local opt-in | **Rejected** | None |
| Delayed live broadcast | Q4MAX/CPMA multiview suggests a natural broadcast path | Correct recipient filtering and durable MVD first; do not label unrestricted live observation as delayed | **Deferred**; broadcaster views and server MVD are supported, but engine Q4TV/repeater transport, delay and live multi-POV are not implemented | Separate relay/delay design after role-filtered observation is proven; connect the existing pure public-only repeater boundary only through a future authenticated transport |

## Why this is one framework, not a mod-feature collage

The cohesive unit is a match operation, not a command name. A player pressing
Ready, a captain accepting a roster invitation, a referee committing rules and
a passed proposal all enter the same pipeline:

`bounded request -> bound participant -> capability and phase policy -> semantic validation -> one commit -> evidence -> recipient view`

This has several openQ4-specific benefits:

1. Quake 4's existing gameplay states and stock assets remain authoritative;
   the framework does not invent a parallel rules engine or require replacement
   content.
2. The engine/game-module boundary stays narrow. Secure random data, MVD and
   atomic filesystem operations are engine services; match policy stays in the
   game module.
3. Casual servers keep their existing behaviour because managed competition is
   enabled by an explicit typed profile.
4. Roles grant only match capabilities. A referee cannot silently become an
   rcon, ban-list or filesystem administrator.
5. UI, console compatibility and proposals cannot drift into independent
   authority systems because they consume the same descriptors and result
   reasons.

## MuffMode lifecycle follow-up (2026-09-05)

The warmup, live-match and intermission comparison used
[MuffMode's match controller](https://github.com/DarkMatter-Productions/MuffMode/blob/5b375b251a28c857def3bcd125bd113bcfe78088/src/sgame/muffmode/mm_match.cpp)
and [runtime exit rules](https://github.com/DarkMatter-Productions/MuffMode/blob/5b375b251a28c857def3bcd125bd113bcfe78088/src/sgame/core/runtime.cpp).
Credit belongs to the MuffMode contributors for this behavioural reference.
The comparison is an independent implementation in the existing Quake 4
code: no GPL-2.0 MuffMode source was incorporated into the SDK-licensed game
repository. openQ4's engine remains GPL-3.0.

| Concern | Finding and resulting behaviour |
|---|---|
| Bot readiness | MuffMode counts human votes separately from total population. openQ4 was using `IsFakeClient()` as a bot test, although it identifies the special view entity. Server match bindings and casual readiness now use the bot manager's slot identity; remote HUDs use the replicated human flag. |
| Population minimum | Added append-only `min_active_players` to rules schema 2, separately from `min_active_humans`; casual `si_minPlayers` includes bots. Arena keeps its entrance policy, and warmup-free practice retains one-human starts. |
| Team presence | Casual `si_teamForcePresent 0` permits an asymmetric minimum, while still requiring opponents on both sides. Red Rover keeps its mode-specific exception. |
| Duel queue | Only seated contenders supply population/readiness; casual Duel requires all human contenders to ready, regardless of the general percentage. |
| Countdown cancellation | The session already cancelled on readiness loss, but readiness operation descriptors excluded COUNTDOWN. Both individual and team operations now admit it. The game reconciles session cancellation before consuming the scheduled start, and accepted ready commands no longer lose their UI mirror to the five-second userinfo throttle. |
| Score-limit ties | DM, TDM and CTF keep the full finishing delay before sudden death. Timed overtime is reserved for clock expiry. The existing session already rejected premature overtime grants; that invariant remains intact. |
| Winner selection | TDM/CTF select the larger score when both teams exceed the limit. DM/Duel and sudden-death leader marking read current eligible players rather than depending on the scoreboard sort cache. |
| Population loss | Existing typed abort/forfeit ownership and untimed-match exit remain. The last active human leaving now aborts instead of allowing bots to continue an abandoned match. |
| Reset/intermission | Warmup clears pending transitions, finishing delays and overtime; GAMEON clears any inherited finishing delay. Existing review duration, rotation, Arena ceremony and round completion ownership remain. |

MuffMode's reconnect reservations, award/map-vote intermissions and configurable
draw deadlines are not imported. Their policy and persistence would require
separate product decisions. Quake 4's Tourney arenas, round modes and Arena
campaign continue to own their specialized progression.

Regression coverage is in `openQ4-game/tools/tests/mp_match_flow_contract.py`
(compiled production score-limit loops, leader selection, bot classification,
session population and countdown reconciliation), the protocol/session/round/
termination contracts, and `tools/tests/mp_match_flow_smoke.py` (windowed stock
`q4dm1`, engine screenshots and phase assertions). The smoke uses explicit
bot creation and direct server kicks; `kickbots` appends its kicks to the
command queue, after any already queued test commands.

Validation on Windows x64 passed the Meson engine/SP/MP build, all 37
competitive contracts, and the engine's competitive-layer, localization,
Match Control UI and key-binding contracts. The extracted production match
flow and real session core also passed Clang AddressSanitizer and
UndefinedBehaviorSanitizer under Linux/WSL.

Live `q4dm1` tests passed warmup with one human and two bots, countdown
withdrawal, a fresh countdown, live gameplay, population-loss review and the
return to warmup. Both opponent removal and the last human choosing spectator
were exercised with `si_timeLimit 0`. The log and four engine-rendered
screenshots are recorded beneath `.tmp/mp-flow-round/`; the successful
last-human run is `smoke-human-final/report.json`; the opponent-removal run is
`smoke-opponents-final/report.json`. These checks do not qualify
the full team/round/Tourney, remote-client or controller matrix below.

Other observations remain separate from this lifecycle change:

- The casual vote electorate's `IsEligibleVotePlayerSlot` still uses the
  special-view `IsFakeClient` predicate alone, so its stated human-only bot
  exclusion needs a separate correction and vote regression coverage.
- Stock `q4dm1` logs retain missing AAS/non-precache declarations, the
  `ammo_shotgun_3` solid-placement warning, and script-path portability
  warnings. They did not prevent the successful lifecycle smokes.
- One earlier smoke process ended abruptly with exit `0xFFFFFFFF` and no
  engine diagnostic; the repeat passed. Its cause was not established.

## Ranking follow-up (2026-09-05)

openQ4's counterpart to MuffMode's `CalculateRanks()` is
`idMultiplayerGame::UpdatePlayerRanks()` in the companion game repository.
[MuffMode's ranking implementation](https://github.com/DarkMatter-Productions/MuffMode/blob/5b375b251a28c857def3bcd125bd113bcfe78088/src/sgame/core/runtime.cpp)
was a behavioural reference for competition places and the separation of
playing clients from spectators. The implementations remain independent;
no GPL-2.0 source was incorporated into the SDK-licensed game module.

This round corrected the following:

- Ties now occupy their full set of places: scores `10, 10, 9, 8, 8` yield
  displayed places `1, 1, 3, 4, 4`. The old dense ranks also made tie detection
  inspect the wrong list neighbours after earlier ties. Queries now derive
  their answer from the score snapshot, independent of mutable player ranks,
  and always initialize the tie result, including for unranked players.
- Player/team comparisons avoid signed subtraction and use the client/team
  slot to order equal scores deterministically. Ordering within a tie does not
  break the tie. Combined personal/team contribution scores retain their
  existing meaning and clamp each counter to its supported range before adding.
- Refreshes retain `idList` capacity and assign ranks in one linear pass
  after sorting, replacing per-refresh allocation and quadratic assignment.
  Invalid entity slots cannot index player state; inactive players lose their
  rank, and unranked notices no longer display “0th place”. The team-score
  accessor also rejects `TEAM_MAX`, previously an out-of-bounds index.
- Duel uses the game state's contender seats for ranking and session
  participation. Physical spectator cameras no longer withdraw its finalists
  during review. Waiting players remain unranked even during a transient spawn;
  ordinary elimination spectators and Tourney byes remain ranked. Loser
  rotation occurs at `NEXTGAME`, before the next warmup resets scores.
  The stock scoreboard and summary receive the compatible DM layout value
  while the displayed mode name remains Duel; the previous unknown value
  produced an empty final table despite correctly populated ranking data.
- Duel reliably transmits its two seats, including changes without a phase
  change and initial state for a new client. Truncated, extended, invalid or
  duplicate-seat messages are rejected before mutating the current state.
  Update clients and servers together; older development Duel demos have the
  previous state layout.
- Individual result recording obtains a fresh rank leader and requires an
  untied lead with at least two contenders and a valid participant binding.
  A cached first-place entry can no longer turn a tie into a decided result.
  Existing team, series, abort and forfeit result ownership remains in place.

`openQ4-game/tools/tests/mp_match_ranking_contract.py` compiles the production
ranking functions, actual `idList`/`rvPair` containers, complete Duel
implementation and base state serialization functions. It covers all ranking
modes, 32-player randomized tie groups, negative/extreme scores, missing and
replaced slots, zero/one-player results, allocation reuse, Duel review/rotation,
initial/delta seat replication and malformed messages. Windows x64 builds,
all 38 competitive contracts, the Linux portability contract and Linux/WSL
AddressSanitizer/UndefinedBehaviorSanitizer execution passed.

The windowed stock `q4dm1` Duel smoke passed warmup, countdown cancellation,
live play, an untied one-minute finish, ten seconds of review and return to
warmup. The log reports two ranked finalists and one waiting player. Inspection
of the engine-rendered review screenshot confirmed `Cortez: 0` in first place,
`Player: -1` in second, and no queued bot in the final table. Evidence is in
`.tmp/mp-ranking-round/duel-review/report.json` and the accompanying log/TGAs.
The staged-package DM population-loss smoke also passed, with its evidence in
`.tmp/mp-ranking-round/dm-population/report.json`.
The staged MP DLL matches the built/tested DLL; no stock GUI assets were changed
for the layout fix.

These checks establish the rank calculation and exercised adapters; they do
not replace the full remote-player, Tourney bracket, team/round, controller
and release-qualification matrix below. The earlier stock-asset warnings and
casual vote bot-eligibility observation remain outside this ranking change.
Review also logs an existing empty-key `idDict::FindKey` warning; it does not
prevent the final table from displaying, but remains a separate UI cleanup.

## Voting, configuration and round turnover follow-up (2026-09-05)

The next MuffMode comparison covers human option-vote eligibility, optional
ready-up, and repeated rounds. It uses the same independently implemented
behavioural reference and licensing boundary recorded above.

- Both inherited vote transports now exclude actual bot-manager slots. The
  special-view `IsFakeClient()` check remains separate. Ineligible callers
  cannot seed a legacy vote's automatic yes ballot. Frozen electorate,
  disconnect, slot-reuse, strict-majority and delayed execution behaviour is
  exercised in `mp_match_vote_electorate_contract.py`.
- The live casual-rule import now sets the threshold to zero when ready-up
  is disabled. Previously `si_useReady 0` or `si_warmup 0` retained a nonzero
  threshold and failed the rule core's cross-field validation. A zero casual
  percentage also disables the gate outside Duel. The regression compiles
  the production importer, complete rule core, gametype table and actual
  CVar defaults across every selectable mode and warmup/readiness combination.
- Between rounds, the server sends a new payload-free
  `GAME_RELIABLE_MESSAGE_ROUNDRESTART`. Remote clients restart world entities
  without `SetGameType()` destroying their current match state. Wrong mode,
  wrong parent phase and trailing payload are rejected. Ordinary full map
  restarts retain their existing meaning. Update clients and servers together.
- The reliable receive range now ends at an append-only count sentinel. The
  previous upper bound rejected announcer messages and the new round-reset
  message before dispatch. The compiled regression includes the real wire
  enum, range guard and round dispatcher, including invalid IDs and payloads.
- Original CTF maps lack neutral flags. Both peers now prepare a deterministic
  extra map entity using the existing neutral-flag definition at an unteamed
  pickup location, minimizing the longer straight-line distance to the two
  bases. Authored neutral flags take precedence, explicit One Flag filters
  override the inherited CTF filter, and the flag cache includes a bounded
  neutral slot. No additional content assets are shipped. This is a playable
  fallback, not proof of equal travel time or competition balance on every map.
  The compiled regression covers placement, authored precedence, name
  collisions, repeat loads, filters and cache bounds. Matching client/server
  builds are required; replay older CTF development demos in their recording
  build because the initial entity list changes.
- `openq4_reportMPState` provides server-local, read-only phase, round, score
  and player diagnostics for engine-scripted tests. It does not inject input.
  `mp_match_flow_smoke.py` now checks game/session agreement, two-round
  scoring, optional option votes and flag captures. The new
  `mp_round_remote_smoke.py` coordinates two hidden windowed clients using
  temporary engine `exec` files and the registered `screenshot` command.

The Windows engine/SP/MP build and staging passed. All 46 competitive
contracts pass; the neutral-map and reliable-receive regressions also pass
Linux/WSL AddressSanitizer and UndefinedBehaviorSanitizer.

Runtime evidence is under `.tmp/mp-gametypes-round/`:

| Evidence directory | Result established |
|---|---|
| `tdm-population`, `deadzone-population` and the four `*-population` flag cases | Warmup, countdown withdrawal, gameplay, opponent loss and return to warmup |
| `ft-first`, `rr-first`, `ca-rounds-final` | Two rounds, a 2-0 result and return to warmup |
| `ca-remote-range` | Two real clients, readiness disabled, remote world reset retaining the match object, respawn into round two and a 2-0 review on both peers |
| `dm-vote-enabled` | One human among two bots passes and executes a warmup timelimit vote |
| `ctf-captures-final`, `actf-captures`, `oneflag-captures`, `a1fctf-captures` | Two actual flag captures, 2-0 review and return to warmup on stock `q4ctf1` |
| `oneflag-remote` | Two real clients populate the same stock One Flag map and see a 2-0 capture result |
| `ft-rescue-fixed` | A frozen teammate thaws after uninterrupted nearby contact, the rescuer earns one point, and two rounds finish with a 2-0 review |
| `deadzone-control-final` | A token carried into the original `q4dz1` zone scores five seconds of control, finishes 5-0 and returns to warmup |

Engine-rendered screenshots confirm the remote round-two HUD and final
scoreboard. Earlier failed fixtures remain for diagnosis: the round test
initially requested a countdown below the four-second minimum; the option
vote omitted `si_allowVoting 1`; flag teleports needed `net_allowCheats 1`;
and Tourney's three-player bracket gave the local player a bye, invalidating
the generic active-player assertion. These test corrections are separate
from game fixes.

Freeze Tag qualification found two gameplay defects. The normal overkill
path gibbed frozen bodies, including on snapshot clients. The rescue trace
then hit the rescuer's own solid player box, blocking otherwise clear
approaches. Freeze Tag now preserves the body and traces from the rescuer's
eye position while ignoring that player; intervening world geometry still
blocks rescue. The production visibility function and thaw timer are compiled
in `mp_match_freeze_rescue_contract.py`, covering contact, interrupted progress,
blocked geometry, missing collision worlds, invalid rescuers and unattended
thawing. Windows and Linux/WSL ASan/UBSan pass. The engine screenshot shows
the localized successful-rescue notice.

DeadZone now recomputes control from eligible live players, counts controllers
in token-free authored zones, handles a first touch losing its token, treats
opposition consistently across entry orders, and stops individual scoring
outside live phases. The compiled production-controller regression covers
these cases, and the stock objective was replayed with the updated binary.

Tourney's four-player bracket passes the corrected lifecycle fixture in
`tourney-final`: both semifinals complete, the expected finalists play, the
expected champion wins and the server returns to warmup. The earlier
`tourney-isolated` assertion sampled after readiness-disabled warmup had already
advanced into the next countdown. The fixture now checks the recorded lifecycle
edges. The reset sentinel uses its signed-byte writer with unchanged wire bytes.

`rr-managed-isolated` completes the competitive profile's eight rounds with two
real clients, seven remote resets and an 8-0 review on both peers. Its saved
schema-2 report has 32 ordered events, no drops and a completed linked MVD.
Managed Red Rover uses a dedicated authoritative conversion path; ordinary
admission remains gated. The compiled core preserves locked roster seats and
roles through full-server conversions and rejects stale, disconnected,
inactive, paused and wrong-phase requests. Text/voice routing follows the
current gameplay side; preparation restores declared roster sides.

This run also exposed and fixed managed startup defects: the listen host's
auto-join intent was consumed before warmup existed, and operator force-ready
entered countdown without scheduling gameplay. The two-client runtime now
proves both paths; the compiled flow test covers direct countdown entry.

Concurrent staging interrupted earlier repeats. Qualification now uses a
hash-verified runtime copy under a separate executable name, retaining the
original failed fixtures and source/copy manifests. A later ordinary stage was
deferred because another running game held the package archive open.

This does not yet establish complete competition parity. The broader role and
competition matrix below still requires qualification.

## Remote admission and replay follow-up (2026-09-06)

The managed startup adapter could broadcast a newly spawned remote player's
placeholder userinfo before that client's actual userinfo arrived. This consumed
the one-shot join choice and left the client requesting spectator mode while
the server had admitted it. The adapter now waits for the initial userinfo,
publishes accepted startup decisions after the current caller finishes, and
broadcasts game-initiated roster/team changes to the owner as well as peers.
The compiled userinfo regression covers initial admission, missing userinfo,
publication order, duplicate participants and invalid bindings.

`control-captain-final` and `control-captain-4x3` pass automatic admission, roster
configuration, captain assignment, force-ready, technical pause, remote captain
timeout, resume and abort with two real clients at 960x540 and 800x600. The
accepted match clock stays frozen while the engine clock advances. Cold menu
activation ranges from 788 to 2445 ms in these runs. The fixture now honors the
menu's initial and outgoing transitions and privileged-operation cooldowns.

Visual review exposed clipped status text and off-screen committed rule values.
The status/evidence summaries now wrap, the Rules table uses the panel width,
and the participant selector avoids concatenating hidden columns. The interface
uses the shipped chamfered frame and button plates. A later busy-machine repeat
also demonstrated why pause assertions must wait for accepted simulation-clock
progress rather than assuming wall-clock waits finish the resume countdown;
the fixture now measures accepted engine-clock progress and retries atomic
script publication when Windows briefly holds an executing file open.
`control-layout-4x3-final` passes with the final layout at 800x600, including
captain assignment, both pause paths and abort. Its engine captures confirm
readable status text, committed rule values, roster controls and evidence.

`control-duel-baseline` exposes a separate managed Duel admission defect: the
legacy contender list is still empty when synchronization revisits an accepted
join, so it revokes the session seat before Duel can promote it. Synchronization
now retains accepted managed Duel membership. `control-duel-timeouts` passes
automatic admission, force-ready, operator technical pause, the remote
contestant's timeout, resume and abort at 960x540. Each accepted contestant can
spend only their own timeout budget and resume their own pause; they gain no
captain or team-management role. The interface identifies these budgets as
Contestant A/B. Native regressions cover accepted membership, absent/stale
bindings, inactive players, wrong-side requests and independent budget charges.
Managed Duel queue turnover with a third waiting human remains unqualified.

`rr-mvd-final` passes the combined replay workflow in the saved eight-round Red
Rover match: both players can be selected repeatedly while paused, free flight
and following can be switched without advancing the playback clock, forward
seek and rewind retain the camera across world resets, frame stepping refreshes
the view, and playback consumes the clean end record. Explicit selection and
saved-camera restoration have separate meanings in the existing offline-demo
interface so transient round-reset state does not discard the saved camera.
Seven engine-rendered captures verify the library, controls and camera states.
The later round-end capture also shows distorted foreground body/debris
geometry; that rendering issue is separate from this control-flow pass.

All 48 competitive contracts pass with the final camera and menu projection
changes. The camera, userinfo, operation and menu-model regressions also pass
Linux/WSL AddressSanitizer and UndefinedBehaviorSanitizer. The Windows client, dedicated server and both
game modules build successfully. Remaining runtime and staging checks below
still prevent a complete parity claim.

Remote-role qualification found that the live network engine omitted the game's
`ClientRun` and `ClientEndFrame` hooks. Referee authentication reached its pending
challenge but never processed the response queued for `ClientRun`. The engine
now calls both hooks once around each live client's prediction batch, including
presentation frames with no prediction tick. The compiled production frame loop
passes 200 connection, timeout, userinfo and timing cases on Windows and Linux.
`control-roles-clientframe` passes the combined three-client workflow at 960x540:
coach assignment, roster departure, broadcaster grant/revocation, referee
sign-in, technical pause/resume, sign-out, operator pause, remote captain timeout
and abort. All three clients exit cleanly; 30 engine-rendered captures are
retained. Menus activate in 1089–1238 ms.

The role fixture decodes serialized accepted recipient views with the production
MatchView codec. These files represent the view already received by each client;
they are not packet captures. A server-local read-only diagnostic checks live
camera permissions separately, because the remote client's local query cannot
grant server-owned follow authority. Actual keyboard/controller camera cycling
remains a separate qualification step.

The coach receives own-side vitals and one teammate follow target, without item
timings. Broadcaster and referee views contain both active players and the three
registered major items on `q4dm1`; the server permits both player cameras.
Roster departure, broadcaster revocation and referee sign-out each return the
recipient to a neutral spectator with no live vitals, item timings or follow
targets, and both server camera permissions are denied. `control-roles-4x3`
also passes at 800x600 with 30 captures and menu activation in 764–1262 ms.
Visual inspection still finds clipping at the end of the long live readiness
reason in some 4:3 role captures; that text-layout edge remains open.

The first BO1 runtime (`series-bo1-first`) reached the decider, then exposed a
projection mismatch: the core stores the decider-confirming side for its audit,
but the public map summary requires no picking side for a forced decider.
Normalizing that field preserves the separate actor in veto history. The
production projection now passes the compiled map validator for both actors,
ordinary picks, deciders and every starting-side state. The network session
dispatcher also now queues the game's exact `nextMap` request instead of
ignoring it; its compiled regression passes Windows and Linux. The live map
handoff now reaches the selected stock map with both humans admitted. The
fixture was corrected to observe map initialization rather than expecting its
one-shot startup hook to run again.

`series-bo1-handoff` reached gameplay and accepted a forfeit, then uncovered a
result-publication lifetime bug: the MVD artifact input referenced a path in a
local engine-result object after the projection returned. Caller-owned path
storage now survives through `AppendMapResult`. Pending paired checkpoints also
keep the accepted match view available instead of projecting a persisted but
unfinalized journal as an invalid lifecycle. Map shutdown now seals evidence
before clearing the outgoing map name and player entities. The compiled live
adapter checks pass 16 teardown cases, 10 MVD artifact states and four lifecycle
projections on Windows and Linux, including Linux ASan/UBSan.

`series-bo1-report` commits the map result and linked MVD without those warnings.
Review closes the gameplay menu, so the fixture now reopens Match Control using
the game's normal menu request before advancing. A later traced repeat exposed
a separate disconnect during the synchronous countdown-to-play world reset:
with the server stalled at frame 1837, the peer predicted through frame 2093 and
was disconnected on the next command outside the 256-frame history window.
The server now discards out-of-window commands without decoding them into its
history, allowing the next snapshot to resynchronize the client. Malformed
counts, negative histories and truncated packets retain their rejection path.
The compiled production handler passes Windows and Linux; network-security
contracts also pass.

`series-bo1-window` keeps both peers connected through live play, forfeit and
review, with the accepted score at 1-0 and the MVD committed. Its final Advance
exposes a terminal adapter gap: the operation core advances to COMPLETE and
requests persistence, but that continuation finalized a report only for
CANCELLED. It now finalizes both terminal states before capturing their paired
checkpoint. The compiled production continuation passes 20 state, failure and
authorizer cases on Windows and Linux, including Linux ASan/UBSan.

`series-bo1-reliable-trace` completes the full two-human BO1 on stock `q4dm5`:
both peers accept live play, forfeit, review and the terminal 1-0 series state.
The fixture validates the final series JSON, matching session/rules identities,
exactly one result in an ordered lossless journal, and committed nonempty MVD
links with no pending files. Both processes exit cleanly. The engine capture
showed adjacent Series-tab list columns running together. The later engine
capture in `series-duel-recovery-pump2` verifies readable full-width stacked
lists with explicit column stops and three complete visible rows.

`series-bo3-first` also passes: two humans advance through stock `q4dm3` and
`q4dm4`, preserve the side mapping and distinct match sessions, and finish 2-0
with both reports and MVDs committed. `series-bo1-playback-live` plays the BO1
report's linked 15.408-second recording through its clean end, checks both
follow targets, free flight, forward seek to live play at 12 seconds, rewind to
3 seconds, and stepping. The seven engine captures and all playback checks pass.

The preceding `series-bo1-terminal` run instead lost its remote peer to a
reliable-queue overflow while that peer had not yet sent its new-map userinfo.
Periodic views now wait for game admission; the initial reliable state still
carries its full view. Admission sends the latest revision without replaying
loading samples. A compiled production-loop test holds one peer unadmitted for
60 revisions while proving uninterrupted delivery to the host and another peer,
then verifies one current view when admission completes. The successful
five-map repeat below qualifies this guard across loading and recovery.

`series-bo5-first` commits two results with the admission guard, then its host
asserts in `idBitMsg::CheckOverflow` while sending fragments during the third
map load. Packet logging can recursively offer `PacifierUpdate` before an
outer fragment send returns when logging outlasts the pacing interval. A scoped
loading-update guard now blocks those nested offers and restores availability
after early returns or exceptions. The compiled production client/dedicated
update passes Windows and Linux ASan/UBSan, and removing the recursion check
fails the regression. Existing pacing and dedicated-load checks pass. The full
engine/module build succeeds; the rebuilt five-map repeat below also passes.

`series-bo5-full-recovery` restores its durable checkpoint after both fixture
processes are abruptly stopped at 1-1. Both new connections agree on the score,
map history and current selection, with a new runtime session. It then exposes
a missing continuation: MAP_COMPLETE is recovered into WARMUP, but advancing
required the previous GAMEREVIEW phase. The adapter now recognizes a completed
checkpoint linked to a different runtime session and supplies a server-owned
recovery condition to the operation core. Ordinary early warmup requests remain
rejected. Native tests cover the unchanged preflight, explicit recovery and 27
phase/state/session-identity combinations; the full build passes.
`series-duel-recovery-spaced`
completes its first Duel map and restores the 1-0 review, then identifies a
second gate in the client menu model. Its context preflight now accepts a
completed-map warmup only when the accepted server availability authorizes
Advance; native tests cover ten phase/permission combinations and preserve
caller output on rejected requests.

`series-duel-recovery-client` (runtime v30) now passes the entire two-human
BO3 sweep with an abrupt restart after map one. The restored 1-0 review advances
through the native Match Control actions, fresh explicit Duel bindings permit
the second game, and the final 2-0 report matches both accepted recipient views.
Both map reports and MVDs are committed with distinct match sessions and no
pending artifacts; both peers exit cleanly.

`series-bo5-full-recovery-client` (runtime v30) passes all five stock maps
`q4dm1` through `q4dm5`, alternates winners to finish 3-2, and abruptly restarts
both fixture peers after the second map at 1-1. Both recipient views retain
the recovered score, history and competition-to-game-side mappings. The
fixture validates all five distinct match sessions, sealed map journals,
committed MVDs and the final series report, with no pending result files.
Both processes exit cleanly. Packet tracing remains enabled throughout this
repeat, and neither the recursive loading assertion nor the initial-admission
reliable overflow recurs. Artifact failure isolation is covered in the subsequent
qualification below.

The same captures reveal that GUI-only string choices used numeric indices on
write and read the empty CVar binding on refresh. The choice widget now reads
and writes its declared GUI value while preserving numeric and dual CVar/GUI
bindings. New-map choice initialization reflects the accepted series profile.
Compiled production read/write/selection tests pass Windows and Linux ASan/UBSan.
The `series-duel-recovery-pump2` engine capture verifies the correct Best of
three choice after the selected-map load.
The first Duel recovery fixture stopped on a transient command-file read;
the replacement driver uses an immutable pump and one-shot command variables
so a selector retry cannot repeat an operation. The successful Duel recovery
run qualifies that driver, including the spaced typed binding operations.

The current full build succeeds after a separate concurrent server-browser
compile issue was resolved in the working tree. The runtime repeat uses the
rebuilt engine and modules together. Full staging remains unqualified.

## Browser, Duel and match-status refinement (2026-09-06)

This pass applies the reference mods' player workflows to the existing browser
and typed match system. MuffMode's
[Duel controller](https://github.com/DarkMatter-Productions/MuffMode/blob/5b375b251a28c857def3bcd125bd113bcfe78088/src/sgame/muffmode/mm_duel.cpp)
provides the behavioral reference for stable queue order, frozen final results
and explicit forfeits. Q4MAX's ready, timeout and scoped match-control workflows
inform the presentation. No external source was incorporated: MuffMode remains
a behavioral reference with a separate licence from the engine and game module.

The browser now displays discovered servers through the stock interface,
preserves endpoint identity through sorting and refresh, clears hidden
selections, and saves favorites atomically. Network rows, endpoints and mod
identities are bounded and sanitized; malformed or truncated responses cannot
publish a row. Rapid clicks on different rows no longer trigger a join, Ctrl
selection changes once, and keyboard actions see the updated selection.
Long text fits the existing columns with UTF-8-safe measured ellipses.

Managed Duel now promotes the next waiting human in warmup, preserves finalists
through review, and requires fresh readiness after promotion. Duplicate joins
preserve queue order; ties return both contestants to the queue, while aborts
preserve both seats. Unfinished series retain their pair, and terminal series
release obsolete timeout-side bindings. A leading player's forfeit retires
that player and records the validated opponent as winner while preserving the
original signed scores. Forfeit and winner announcements are localized.
The listen host now accepts fresh recipient views independently of open menus.
Warmup-disabled empty or bot-only servers wait without repeated rejected
countdown transitions.

Match Control omits readiness blockers during live play and removes personal
ready/not-ready text from observers. Complete blocker sentences no longer gain
extra comma punctuation. Action reasons respond to focus and hover, feedback
wraps and scrolls, and its panel fits above Back at 4:3. The result field stays
empty until there is an operation result to show.

Runtime inspection also exposed a lost managed-mode flag: rebuilding the
engine's server-info dictionary discarded a game-only value. The registered
read-only server-info CVar now survives that rebuild; clients prefer committed
rules from a validated current recipient view after the handshake. Managed team
summaries use neutral MARINES/STROGG headings, because raw score order cannot
identify an aborted or forfeited game's winner. A separate terminal-result
projection for richer persistent summary headings remained future work at this
checkpoint; the later persistent-outcomes implementation below supersedes
that limitation.

Retained evidence is under `.tmp/mp-refinement/`:

| Evidence | Result |
| --- | --- |
| `build-final7.log`, `build-final8.log`, `stage-final8.log` | Windows engine, dedicated server and both game modules build and stage successfully. `runtime/manifest.json` records SHA-256 hashes of the isolated runtime used for final repeats. |
| `contracts-final3.log` | All 52 competitive contracts pass. Browser, real list-handler, network, GUI and six-language encoding contracts also pass. |
| `summary-identity-windows.log`, `summary-identity-linux-sanitizers.log` | Actual managed-mode predicate, rule mirror and summary builder pass, including stale recipient identities and server-info rebuilds. New Duel queue/result and local-view regressions also pass Linux ASan/UBSan. |
| `browser-final-gl/report.json`, `browser-final-vk/report.json` | Real LAN discovery, visible browser, sorting, filter clearing, saved favorite changes, module switch and stock-map gameplay pass on both renderers. Four engine captures; all processes exit cleanly. |
| `duel-final2/report.json` | Three-client leading-player forfeit, review retention, FIFO promotion, fresh readiness and accepted listen-host state pass. The persisted report contains one forfeit result, the actual winner, and original -1/0 scores. Seven engine captures; all three clients exit cleanly. |
| `control-final-gl2/report.json` | 800x600 player/coach/broadcaster/referee role and revocation matrix passes with 30 engine captures. Menu activation takes 1115-1286 ms. This run predates the remote managed-mode correction; its remote summary-heading defect is covered by the final Vulkan repeat below. |
| `control-final-vk3/report.json` | Final 960x540 three-client role, pause/resume and abort run passes with 30 engine captures. All peers expose and render neutral MARINES/STROGG summary headings. Menu activation takes 1866-2000 ms; all three clients exit cleanly. |

The fixtures decode recipient views already accepted by the actual clients with
the production codec. Browser and GUI checks invoke engine menu handlers and
read existing GUI variables; they do not inject operating-system input. All
games are hidden and windowed, and images come from the engine `screenshot`
command. A parallel role run exhausted its 45-second cold-start admission wait
immediately after the server admitted its peer. The final repeat runs serially
with a separate 90-second startup allowance; its three-second menu budget is
unchanged. An earlier overlapping-port fixture was stopped before joining and
is excluded; the runner now accepts an explicit port range.

These results qualify the named scenarios, not full reference-mod parity.
At this checkpoint, physical keyboard/controller interaction, broader
supported-mode and recovery coverage, live spectator camera interaction and
tournament qualification remained open. Later sections record additional
recovery and camera evidence without claiming physical navigation or tournament
certification. Friends/Find Player/Clan service commands and server-hover
tooltips are still unsupported. Map-cycle script discovery emitted malformed
prefixed absolute-path warnings, subsequently fixed in the persistent-outcomes
follow-up below; stock non-precache warnings remain separate. The observed
Duel turnover succeeded despite those warnings. Live Q4TV/repeater transport
and delayed broadcast remain deferred as described in the reference matrix.

## Repeated pause and artifact publication checks (2026-09-07)

`series-evidence-failure` (runtime v32) passes a two-human BO3 on stock
`q4dm3`/`q4dm4` with the first map's final JSON destination deliberately blocked
by a nonempty directory. Both peers complete the series at 2-0. The final
series report marks that JSON failed with reason 9 and no usable qpath, retains
both committed MVDs, and records the second map's valid JSON. The blocker is
unchanged, no pending result file remains, and both processes exit cleanly.

`series-mvd-failure-recovery` (runtime v33) passes another two-human BO3 with
the first map's final MVD destination blocked. It retains a nonempty
`.mvd.part`, records failure reason 265 in the series artifact row and one
`mvdStop` failure in that map's JSON, and does not advertise a playable link.
Both fixture processes are abruptly stopped after the first review; recovery
preserves the 1-0 score and failed artifact status. The next map publishes its
JSON and MVD, both peers accept the final 2-0 result, and both exit cleanly.

The repeated world-pause fixture checks native player/weapon animation clocks,
a stock rocket projectile, a rotating stock model, a collected placed armor
item and a timed powerup. `pause-world-resumed` exposes a remote-only 1024 ms
animation jump during its second pause: client snapshot correction can advance
the clock by more than one prediction tick, while deadline rebasing used only
one tick. Rebasing now follows the entire new prediction interval and excludes
replayed frames; joining or seeking establishes a fresh one-tick baseline.
The compiled production clock regression fails before the fix and passes after
it on Windows, Linux and Linux ASan/UBSan.

`pause-world-pinned-clock` (runtime v33) passes both complete pause/resume
cycles on stock `q4dm1`: the 28-second and 3.5-second holds leave host and
remote animation samples unchanged, freeze both accepted match clocks, and
retain the host's motion, weapon, powerup and placed-item state. Both resumptions
advance projectile/mover motion and animation. After sufficient gameplay time,
the armor respawns, the powerup expires and the projectile's posted removal runs.
All eleven engine captures are retained, and both peers exit cleanly. Round and
Tourney countdown coverage follows below.

`pause-world-snapshot-clock` stops before gameplay because the independently
compiled test decoder picked up a concurrent wire-schema update. Its captured
bytes decode successfully with the production codec from v33's staged source.
Copied test runtimes can now supply a pinned decoder; the repeat uses that
decoder and a hashed source snapshot. This fixture/version mismatch is excluded
from gameplay results, as is the earlier user-paused run.

`pause-clanarena-native-clocks` and `pause-tourney-contestants` (runtime v35)
pass two-human managed match-countdown and round/arena-countdown holds of
15 seconds each. Clan Arena retains exactly 6032 ms of round countdown;
Tourney retains exactly 11432 ms before its arena opens. Both recipients
observe the frozen phases, both humans enter gameplay after resumption, and
a third live pause can be aborted into unpaused review. Both runs exit cleanly
and retain twelve engine captures each.

The first Clan Arena attempt identified a diagnostic error: `round` read the
server-only session controller even on the client. The corrected diagnostic
reports the actual round game state and distinguishes the server session field;
the fixture also decodes the accepted recipient's round. The first Tourney
attempt assumed that admitted warmup contestants already had active bodies;
Tourney intentionally keeps them spectating until their arena opens. The repeat
requires accepted intent to play and admission in warmup, then active player
bodies in the resumed arena. Neither earlier fixture failure is a gameplay
regression.

`series-paused-forfeits-choice-debug` (runtime v35) passes both map forfeits
while technically paused, clears the pause on entry to review, and recovers
after the first map before completing the BO3 at 2-0. Both accepted recipient
views agree with the final series JSON; both map reports and MVDs are committed,
and both peers exit cleanly. Its overview captures initially appeared to have a
short series-profile label; the full-resolution pixel checks below correct
that interpretation. The subsequent `series-text-only-probe` run was stopped at the user's
pause request and is excluded from completed-series qualification.

`series-text-probe-resumed` (runtime v37) completes another BO3 at 2-0 with
both peers exiting cleanly. Its apparently short-label captures occur while the native
choice and line renderer both receive the full `Best of three` text, measured
at 71 units within a 166-unit text rectangle.

Full-resolution inspection corrects the initial label diagnosis: the original
TGA and converted PNG regions contain the complete `Best of three` label. The
previously suspected review and shared-GUI captures have identical label pixels
(77-pixel visible width), and a magnified crop confirms all characters. Earlier
MVD-failure and paused-forfeit captures have the same complete text. The overview
previews were misread; no series-label rendering defect is established. Temporary
choice, glyph and geometry tracing was removed without changing the text renderer.

`series-bo3-library-playback` (runtime v37) validates the first map's saved
journal and discovers its linked 17-second MVD in the Demos library as the
selected playable recording. The native diagnostic invokes the same engine
Play action as the menu button. Playback switches between both recorded humans,
enters free camera, follows again, seeks to 12 seconds, rewinds to 3 seconds,
steps and consumes the clean end record. Seven engine captures are retained,
including the visible library and playback controls; the process exits cleanly.
This proves the library action and playback path without physical input.

All 52 current match/security contracts and the additional public-gametype
selectability contract pass after the library diagnostic is built. The runtime
decoder remains pinned to each copied build's production schema.

## Persistent outcomes and spectator controls (2026-09-07)

Managed match results now use a bounded schema-4 public projection frozen from
the authoritative terminal transition. Evidence export consumes the same
canonical result. The outcome remains available through review, nextgame and
the following warmup; the next countdown or replacement session clears it.
Individual winner names and identities survive disconnects and reused slots;
team winners retain their gameplay team independently of series side mapping.
An automatic Duel-series departure also retains its validated surviving winner
instead of being exported as an abort after the departing player is unbound.

The summary shows the actual winner, forfeit, draw or abort above the unchanged
score rows. It refreshes when a delayed accepted result arrives. Personal
victory audio requires a matching frozen individual winner identity, runs once
per result, and never derives a late joiner's historical outcome from current
scores or team membership. Match Control's scrolling Status text exposes the
retained last result during the following warmup.

Spectators can request next, previous, free camera or a specific client slot.
The server checks the owning player, bounded request, target spawn identity and
existing camera policy; losing a coach, broadcaster or referee role releases
an unauthorized followed target. These controls preserve eligible bot targets
and the existing Tourney arena rules. The Status page adds visible previous,
next and free-camera actions using fixed handlers.

Native Windows and Linux ASan/UBSan checks cover the actual result adapter and
codec, frozen name sanitization, malformed and immutable result records, summary
identity/audio handling, follow transport and camera policy. The full competitive
suite passes 53 contracts before the final visible-button integration. The
maximum legal encoded view uses 7,728 of its 7,936-byte cap, leaving transport
headroom in the 8,192-byte reliable-message buffer. Client and server builds
must agree on schema 4.

Initial full build and staging pass. `duel-forfeit` passes three-client gameplay:
the score leader forfeits, all summaries identify the actual winner, saved JSON
agrees, the winner stays and the FIFO challenger advances. `duel-late-join` passes
with evidence disabled: the remote winner leaves, participant 4 reuses the old
slot, and the retained result still names winning participant 2. These initial
runs precede the final Status controls and last-result line; their captures do
not qualify that final UI. Evidence is under `.tmp/mp-outcomes-20260907/`.

Normalizing the full lexer filename before removing its basename fixes root
script include resolution in both parser copies. Windows and Linux sanitizer
fixtures cover VFS-root, nested, archive, drive, UNC, POSIX and unmapped paths;
the old ordering fails the executable regression. The initial Duel turnover
compiles stock `mapcycle.scriptcfg` and restarts successfully without the former
prefixed absolute-path warnings. Stock asset warnings remain separate.

The final visible-control and retained-result scenarios now have passing
runtime reports, detailed below. Those runs do not close physical navigation,
every layout state, or the separate chat-clipping check.

## Current qualification evidence (reviewed 2026-09-07)

The reports below were inspected directly: each records `status: pass` and an
empty failure list. The outcome/control reports also record `stage: complete`;
their launched peers all exit with code 0. These are scoped results from their
copied runtimes, not an assertion that every concurrent source change has been
rebuilt and tested. Retain the dated investigations above when interpreting
earlier failures and superseded qualification statements.

The current outcome and spectator evidence is under
`.tmp/mp-outcomes-20260907/`:

| Report | Proven scenario and limits |
| --- | --- |
| `control-gl-final/report.json` | Team DM at 800x600 with six accepted role/revocation views, 17 camera checkpoints and 30 engine captures. Checks cover coach own-side restriction, ordinary spectator denial, broadcaster next/previous/direct/free camera and wrap, and coach/broadcaster/referee revocation. The panels invoke native menu action handlers; this is not physical-input coverage. |
| `control-vulkan-resumed/report.json` | The same three-client role and 17-checkpoint camera matrix passes at 960x540 on Vulkan, with 30 engine captures. Menu activation takes 1614-1870 ms against the unchanged 3000 ms budget. This closes the named Vulkan spectator repeat previously pending. |
| `duel-vulkan-final/report.json` | Three-client managed Duel passes 15 accepted-view checkpoints and retains eight engine captures at 960x540. The score leader forfeits, the winner stays, duplicate queue joins preserve FIFO order and the promoted challenger must ready. With evidence export disabled, participant 4 reuses the departing winner's slot while the frozen result still identifies participant 2 and its maximum-length name. All three accepted views clear the result at the next countdown. These captures retain a chat-clipping concern; they do not qualify its subsequent fix. |

Additional passing reports under `.tmp/mp-gametypes-round/` establish the
following earlier gameplay and persistence scenarios:

| Reports | Proven scenario and limits |
| --- | --- |
| `pause-world-pinned-clock/report.json` | Two pause/resume cycles preserve accepted match time and remote animation, plus host projectile, mover, weapon, powerup and placed-item state; resumed gameplay reaches the expected respawn/expiry/removal deadlines. This is concrete world-clock coverage, not every mode/deadline combination. |
| `pause-clanarena-native-clocks/report.json`, `pause-tourney-contestants/report.json` | Match and round/arena countdowns remain frozen on both peers, resume into two-human gameplay, and a further live pause can be aborted into unpaused review. |
| `series-bo1-reliable-trace/report.json`, `series-bo3-first/report.json`, `series-bo5-full-recovery-client/report.json` | Team DM BO1, BO3 and full-distance BO5 complete with map/series evidence. BO5 records an abrupt two-process restart after map 2 and a new recovered session; both final process exits are 0. |
| `series-duel-recovery-client/report.json` | Two-human Duel BO3 completes after an abrupt restart following map 1 and explicit connection-scoped recovery. The recovered session differs from the original; both final process exits are 0. |
| `series-evidence-failure/report.json`, `series-mvd-failure-recovery/report.json` | A blocked map-JSON destination does not stop the series. A blocked MVD destination retains the failed recording's `.part`, withholds a playable link and preserves artifact status through interruption/recovery. Subsequent map artifacts publish successfully. |
| `series-paused-forfeits-choice-debug/report.json`, `series-text-probe-resumed/report.json` | BO3 completion, paused forfeits and the recorded recovery path pass. Magnified original-image and pixel-comparison checks confirm the complete series-profile label; the earlier overview interpretation was incorrect. |
| `series-bo3-library-playback/report.json` | The Demos library's native Play action opens the linked generated MVD; playback follows both recorded humans, enters free camera, seeks, rewinds, steps and consumes a clean end record. Seven engine captures and a clean exit are recorded. Physical menu navigation remains untested. |

Mode-specific passing reports in that directory also include
`tourney-even-bracket`, `ctf-captures-final`, `oneflag-captures`, `actf-captures`,
`a1fctf-captures`, `deadzone-control-final`, `ca-rounds-final`, `ft-rescue-fixed`,
`rr-managed-isolated` and `dm-vote-enabled` (each has `report.json`). These prove
their named bracket, objective, round, rescue, isolation or voting scenarios;
they do not establish every mode's readiness, roster and proposal edge.

## Remaining release closure order

Runtime checks still uncover functional gaps as well as qualification work.
The named role/camera, pause, series, recovery and playback scenarios above are
already recorded; target the remaining edges instead of treating those entire
features as untested:

1. Re-run the complete automated contracts
   and staged MP build after final adapter changes, then repeat affected live
   scenarios against that exact runtime. Keep existing stock-asset warnings
   distinct from build or gameplay regressions.
2. Qualify the captain roster workflow: invitation acceptance/expiry,
   full/locked/live joins, substitutions, bench transitions and disconnects.
   Extend authority, referee authentication and filtered-view checks across
   replacement connections, slot reuse and map/session changes. Never infer
   restored authority from a reused slot, name or address.
3. Exercise global/side proposal display and thresholds during disconnects,
   stale/duplicate requests and execution-time revalidation. Qualify series
   pool bounds, insufficient pools, out-of-turn/stale veto and side choice,
   and failed map-load rollback through Match Control; successful series
   completion and process recovery do not cover these failure paths.
4. Complete every role's localized menu workflow and physical
   keyboard/controller navigation, including focus, disabled explanations and
   credential clearing. Extend the existing 16:9/4:3 captures to remaining
   HUD, scoreboard and menu states. Engine diagnostic invocation of a menu
   handler does not qualify physical input; any input control requires the
   user's specifically scoped authorization.
5. Extend supported-mode lifecycle coverage to remaining readiness blockers,
   countdown cancellation, roster/disconnect invalidation and round edges.
   Qualify simultaneous/duplicate timeout requests and once-only budget
   charging. Combine item pickup/respawn with remaining role/lock/connection
   transitions, and cover stale camera targets and captain spectator-invitation
   denial. Keep synthetic repeater policy checks separate: no live
   Q4TV/repeater transport exists to qualify.
6. Qualify remaining interrupted-write/capacity and recording-stop boundaries.
   The [dated MVD qualification](mvd-qualification-2026-09-07.md) adds passing
   manual-recording ownership, automatic start-failure and size/duration-limit
   scenarios, plus corrected paused restart-seek cameras and a matched staged
   playback run. These and the earlier blocked final destinations do not cover
   every filesystem failure mode.

The working tree should therefore be described as an implemented competitive
framework with substantial scoped live evidence and specific release checks
still open, while stopping short of an independent tournament-certification
claim.

## Summary chat clipping qualification (2026-09-07)

The shared GUI clipper could produce a negative height or width for glyphs
wholly above or left of a scrolling viewport. Those inverted quads also used
extrapolated atlas coordinates, producing stray text above summary chat. The
clipper now rejects them before UV interpolation and rendering. It preserves
partially visible glyphs, reversed atlas coordinates and the existing base-clip
exception used by widescreen GUIs.

`gui_clipping_contract.py` executes the production clipper, coordinate
transform and glyph submission path for 2,723 cases. Windows and Linux
ASan/UBSan runs pass; the saved pre-fix implementation fails. The full Windows
build and staging pass in `build8.log` and `install8.log` under
`.tmp/mp-outcomes-20260907/`. The copied `runtime-clipping/manifest.json`
records the tested artifact hashes.

Both `clipping-vulkan/report.json` (960x540, evidence disabled, winner rejoin)
and `clipping-gl/report.json` (800x600, saved JSON evidence) pass with clean
process exits. Engine-written summary captures visibly retain the wrapped
maximum-length winner name, the forfeit line and clean chat bounds. The
registered `uiFontParitySelfTest` also passes in these TrueType-font runs;
its retail-atlas-specific cases are explicitly skipped.

## Roster authority and failure-path fixes (2026-09-07)

The follow-up audit found and corrected three gaps beyond the summary work:

- Captain invitations previously checked only that their issuer was connected.
  Acceptance, substitution and recipient projection now require a current
  active captain on the same side, a current referee, or a trusted listen-host
  grant. The host grant is private server state. Frame cleanup releases stale
  reservations; it cannot transfer authority to a replacement connection.
- Roster acceptance and role assignment now advertise warmup only, and
  substitution advertises warmup/countdown. Review and next-game planning
  agree with those restrictions. Inactive coach/substitute self-withdrawal
  remains available.
- Failed, expired, canceled and phase-invalidated proposals previously kept
  their scope occupied indefinitely. The adapter now records the terminal
  evidence and acknowledges those records. It preserves cooldowns and leaves
  passed proposals on their existing once-only execution path.

Series scheduling also checks the actual `maps/<token>.map` before committing
`MAP_ACTIVE`, using the engine's normal discovery policy without scheduling
addon changes during the check. Missing assets retain `READY` and the selected
map for retry. Failed persistence leaves the published state and map handoff
untouched. This is preflight protection; corrupt data and failures after that
check still require a broader rollback design. The related engine discovery
fix closes the file handle when a loose file is found.

The copied `runtime-roster/manifest.json` under
`.tmp/mp-outcomes-20260907/` records the package and expanded production decoder.
`build9.log`, `install9.log` and `contracts-roster.log` retain successful full
Windows build, staging and all 54 competitive contracts. The new proposal
regression covers four terminal outcomes across three scopes, and the actual
map scheduling regression covers 20 cases plus two restored-asset retries.
Targeted Linux native/sanitizer checks also pass; restoring the previous
invitation/phase checks, omitting proposal cleanup or disabling map preflight
reproduces each corresponding regression. The engine file-discovery regression
passes on Windows and Linux ASan/UBSan and fails against the saved pre-fix code.

`roster-gl-final/report.json` passes 32 accepted-state checkpoints in 448.6
seconds; `roster-vulkan-final/report.json` repeats them in 469.6 seconds. All
five client lifetimes in each run exit cleanly. A valid captain invite
creates a coach; demotion and benching remove outstanding invitations; an old
menu acceptance leaves the target unrostered. A canceled global proposal frees
the scope for a new proposal with the expected electorate and a fresh ID.
Review rejects acceptance, assignment and substitution without changing either
target, and returns to the same session. Replacements reuse the departed
captain/referee slots with new participant and binding identities; they inherit
neither authority nor private vitals, item timing or camera targets. The engine
also denies live camera access to the replacement neutral spectator.

The earlier `roster-gl/report.json` stopped after 25 accepted snapshots because
the fixture tried to select a Match Control row while review's summary GUI was
active. All clients exited cleanly; that run is partial evidence. The corrected
fixture reopens the menu after each accepted review, disables both stock map
cycles and checks session/clock continuity. It invokes native menu actions and
checks accepted bytes; it does not establish physical navigation, successful
two-seat substitution or forged-packet ingress coverage.

The full roster runs' final captures do not qualify Status layout: the inherited
menu helper dispatched `chooseCurr`, which has no route when a fresh menu has
no current panel. The final capture step now also dispatches `chooseDest`.
The focused `roster-panels-vulkan/report.json` run passes in 108.9 seconds with
three clean exits, native visibility readings of 1 and inspected engine captures
of both the active captain/operator and neutral spectator Status panels. This
is a diagnostic routing correction; no product GUI or input-control change was
needed. Earlier OpenGL/Vulkan role-menu captures remain separate visual evidence.
