# Competitive match framework

Date: 2026-08-03  
Status: **Authoritative implementation specification**

This document supersedes
[the 2026-07-28 competitive multiplayer plan](2026-07-28-competitive-multiplayer.md).
The earlier plan remains as an investigation record, including its code-site
inventory, but it is not an implementation checklist. Where the two documents
disagree, this document wins.

Implementation status: the Phase 0–6 code paths are integrated in the current
working trees and the Phase 7 pure/domain, protocol, storage, localization,
presentation and legacy-ingress contracts pass. The warning-clean Windows MP
build and runtime qualification results are tracked separately so this design
record does not turn an automated contract into a tournament-certification
claim.

The goal is one Quake 4-native competitive match system, not a collection of
recognisable mod features. Q4MAX, CPMA, WORR, AfterShock and OpenTDM are
behavioural references: they demonstrate useful player, captain, referee,
observer and operator workflows. openQ4 owns the model, terminology, security
boundary, implementation and presentation described here.

## 1. Outcome and scope

A competitive server has one authoritative match session. The session owns the
committed rules, legal lifecycle, participants and roles, readiness, pauses and
timeouts, proposals, result, optional series, recipient-specific public view and
evidence. Console commands, GUI actions, votes and automation submit typed
operations to that session; none is a second route around it.

The framework must provide:

- individual and team readiness with explicit blockers;
- roster and team locks, captains, coaches, broadcasters and referees;
- team timeouts and referee technical pauses with a safe resume countdown;
- atomic rules profiles, overtime and sudden-death policy;
- typed votes using the same validators and executors as direct operations;
- duel queue and spectator-follow policy;
- optional best-of series, validated map pools and deterministic vetoes;
- role-filtered spectator information and item timing;
- server-side MVD lifecycle, structured results and an audit journal;
- a localized match-control UI, HUD/scoreboard context, correct browser filters
  and task-oriented player/operator documentation.

Defaults preserve today's casual server behaviour. A server opts into managed
competition with a profile; isolated competitive features do not silently turn
the server into a tournament server.

### Supported modes

The first release supports only modes with complete authoritative rules and a
constructible game-state implementation:

- DM, Tourney, Team DM, CTF, One Flag CTF, Arena CTF, Arena One Flag CTF and
  DeadZone;
- Duel, Clan Arena, Freeze Tag and Red Rover.

Overload, Harvester, Domination and Attack & Defend currently have enum,
descriptor, menu or vote remnants but no complete state factory/rules path.
They remain reserved wire values, are marked `implemented = false`, and are
hidden from hosting, voting, browser filters, map compatibility and user docs.
An attempt to select one is rejected with a localized explanation before state
construction. They return only through a mode-specific implementation and
gameplay-validation track; this framework must never claim placeholder modes.

### Explicitly separate work

The following are valuable but are not prerequisites for a correct match
manager:

- lag compensation and engine network-setting enforcement;
- accounts, globally trusted identities and automatic authenticated reconnect;
- tournament brackets, league scheduling and remote web administration;
- delayed broadcast, live multi-POV composition and anti-cheat;
- broad cosmetic enforcement, chat-location macros and forced client
  screenshots.

These concerns must use the session's public APIs later rather than expanding
its authority now.

## 2. Sources and clean-room boundary

The behavioural comparison found a consistent useful core:

- Q4MAX contributes Quake 4-shaped timeout/referee/team-lock/spec-lock,
  multi-POV, demo, stats and menu expectations;
- CPMA contributes captain/team authority, coaching, queues, modes and the
  principle that a competition can be operated without full server control;
- OpenTDM contributes an explicit lifecycle and careful separation of running
  match time from administrative time;
- WORR contributes a modern session hub, readiness flow, series/veto workflow,
  contextual confirmations, MVD and match artifacts;
- AfterShock is useful as a feature inventory and as a warning against magic
  state, unbounded strings, scattered permissions and unsafe report writers.

Licensing is a hard boundary. WORR, AfterShock and OpenTDM are GPL-family code;
Q4MAX and CPMA documentation/assets have their own terms. The Quake 4 SDK-derived
game module in `E:\Repositories\openQ4-game` is governed by the SDK EULA. No
reference source, UI asset, string, prose or distinctive table is copied into
`src/mpgame`. Behaviour is described independently here and reimplemented from
openQ4 and SDK primitives. Reference names may appear in design provenance,
never as a claim that their code was ported. Engine-side GPL compatibility does
not weaken the game-module boundary.

Canonical game changes go only under `openQ4-game/src/mpgame/`; the divergent
`openQ4-game/src/game/` tree is out of scope. Engine, GUI, localization, tests
and documentation changes belong in `OpenQ4`.

## 3. One aggregate, small supporting boundaries

`mpMatchSession` is the sole aggregate root and is owned by
`idMultiplayerGame`. It is created and cleared with the map, advanced once per
authoritative frame and mutated only by validated operations or gameplay
outcome events. Its monotonic `sessionRevision` changes once per committed
mutation.

| Boundary | Responsibility | Must not do |
| --- | --- | --- |
| `mpMatchSession` | Lifecycle, clock, pause overlay, roster, roles, readiness, timeout budgets, active proposal and map result | Parse GUI strings, execute console text or write files |
| `mpCompetitiveRules` | Typed draft, validation, committed immutable snapshot, profile identity and digest | Poll loose cvars during a live match |
| `mpCompetitionSeries` | Optional best-of state, map pool/veto, map results and cross-map recovery record | Own a bracket or alter live map rules |
| `mpCompetitionSeriesReport` | Paired mutable cross-map result draft and immutable final series JSON | Advance independently of the series checkpoint or infer reconnect identity |
| `mpMatchOperationRegistry` | Descriptor schemas, capabilities, phase preconditions, validation and typed executor | Carry a second authority model or call arbitrary commands |
| `mpSessionView` | Versioned, bounded, recipient-specific projection for network, GUI and HUD | Contain secrets or rely on client-side hiding |
| `mpMatchEvidence` | Bounded audit events, MVD lifecycle, stats and atomic final artifacts | Block gameplay on optional output or run a detached writer |

These are conceptual boundaries, not a demand for six directories or a class
per feature. Closely related value types stay together. The earlier proposal's
sixteen independent modules are deliberately collapsed so lifecycle, authority
and policy cannot drift apart.

`rvGameState` and `rvRoundGameState` remain the gameplay-specific phase and
round adapters and retain their existing wire values. Outcome checks request a
transition from the session. After migration, only the session's adapter may
call the underlying `NewState`/round state mutator. Every transition records a
reason and is rejected if it is absent from the legal table.

## 4. Legal state model

### Match phase

The existing `mpGameState_t` values remain authoritative on the wire:

| From | Legal destination | Reason |
| --- | --- | --- |
| `INACTIVE` | `WARMUP` | map and session initialized |
| `WARMUP` | `COUNTDOWN` | the readiness gate is satisfied or a referee explicitly forces it |
| `COUNTDOWN` | `WARMUP` | abort, rules/roster invalidation or loss of a required player |
| `COUNTDOWN` | `GAMEON` | countdown deadline reached with the frozen rules snapshot still valid |
| `GAMEON` | `SUDDENDEATH` | regulation tie and the committed rules disable timed overtime |
| `GAMEON` | `GAMEREVIEW` | decided limit, forfeit or recorded abort |
| `SUDDENDEATH` | `GAMEREVIEW` | decisive result, forfeit or recorded abort |
| `GAMEREVIEW` | `NEXTGAME` | review deadline reached or referee advances |
| `NEXTGAME` | `WARMUP` | same-map restart or next map ready |
| `NEXTGAME` | `INACTIVE` | map unload/session end |
| any | `INACTIVE` | map shutdown or fatal session reset only |

Timed overtime is a numbered live-period overlay on `GAMEON`, as it is today,
not another wire phase. Each tied expiry either starts one validated overtime
period or enters `SUDDENDEATH`; it can never extend the deadline twice. A match
abort reaches review with outcome `ABORTED` and grants no series point. A
forfeit reaches review with outcome `FORFEIT` and identifies the winner and
authorizing principal.

Readiness is a gate inside `WARMUP`, not another competing phase. Its typed
blockers include invalid map/rules, insufficient active humans, unassigned or
oversized teams, vacant locked roster seats, an unfinished veto, and each
eligible participant/team not ready. Spectators and invalid team values are
never indexed or counted as teams.

### Pause overlay

Pause is orthogonal to the match phase:

`RUNNING -> PAUSE_PENDING -> PAUSED -> RESUME_COUNTDOWN -> RUNNING`

A team timeout is legal during active play and is owned by a team; a referee
technical pause may also cover the match or round countdown. The request takes
effect at an authoritative frame boundary. Budget is consumed once when the
pause commits, not when requested or resumed. Only the owning team, a referee
or the server operator can request time-in; policy may require both teams, but
there is still one transition. Abort and forfeit remain available while paused.
All other lifecycle and round advancement is suppressed until resume completes.
Repeated or late requests are idempotently rejected with the current revision.

### Round sub-state

For round modes the existing subordinate states remain:

- `RS_INACTIVE -> RS_COUNTDOWN` when the live parent is ready for a round;
- `RS_COUNTDOWN -> RS_ACTIVE` when its deadline expires;
- `RS_ACTIVE -> RS_COMPLETE` on one recorded round result;
- `RS_COMPLETE -> RS_COUNTDOWN` when the match continues;
- any round state -> `RS_INACTIVE` when the parent leaves active play or resets.

No direct `RS_ACTIVE -> RS_COUNTDOWN` shortcut is legal. Round outcome is
committed before match-limit evaluation. Parent phase, round sub-state and
pause overlay are replicated together so a client never infers an impossible
combination.

## 5. Three clock domains and the idTech4 pause compromise

Every deadline declares one of exactly three domains:

| Clock | Advances while paused | Uses |
| --- | --- | --- |
| Engine/network time | yes | snapshots, prediction, message cadence, pause expiry and resume countdown |
| Match time | no | regulation/overtime/round limits, respawns, movers, projectiles, powerups and gameplay events |
| Host time | yes, independent | UTC artifact stamps, authentication throttles and operator diagnostics only |

The async server and client derive simulation/snapshot time from game frames.
Freezing `gameLocal.time` alone would diverge prediction and snapshot headers.
The implementation therefore keeps engine/network time monotonic and provides
a session-owned match accumulator that advances only while gameplay runs.

While frozen, networking, chat, view angles, scoreboards and match operations
continue. Player movement, attacks, damage and gameplay AI do not. World
entities which pause do not think, and their absolute trajectories, animation
starts, shader time offsets, weapon/player deadlines and posted gameplay events
are rebased by one centralized per-frame freeze pass. Existing physics
`UpdateTime` behaviour is reused where it is the correct trajectory primitive.
Non-gameplay entities explicitly opt out.

This compromise has a strict exactly-once invariant: a deadline is either
expressed in match time, rebased by the central pass, or intentionally belongs
to engine/host time. It is never handled by two. A reviewed deadline registry
and tests cover every absolute-time field touched by multiplayer. Feature code
may not add ad-hoc `pausedDuration` corrections.

The resume countdown uses engine/network time while the world remains frozen;
match time begins advancing on the single transition back to `RUNNING`.

## 6. Transactional rules and supported profiles

One descriptor table defines each competition setting's stable key, type,
range/enum, localization ids, applicable modes, mutability boundary, default
and validation callback. It feeds profile parsing, server console completion,
the match-control UI, vote schemas, docs generation and tests. It does not imply
a new cvar per field.

Rules use a transaction:

1. Start a draft from the current committed snapshot or a named built-in
   profile.
2. Apply typed changes without side effects.
3. Validate fields together, the selected mode, map support, roster size,
   timeout/overtime policy and server bounds.
4. Commit the entire snapshot once, increment its revision and publish its
   canonical digest; on any error, change nothing.
5. At `WARMUP -> COUNTDOWN`, freeze the snapshot for that map. Live changes are
   rejected or staged for the next warmup according to their descriptor.

Existing `si_*` values are compatibility inputs and mirrors at defined
boundaries, not a live alternative source of truth. A changed cvar cannot make
the committed snapshot silently drift. Profiles are typed data, never a list
of console commands and never an arbitrary `.cfg` executed under elevated
authority.

The digest covers canonical field ids and values in stable order, plus schema
version; it is evidence and a connection/display aid, not authentication.

## 7. Principals, roles and typed operations

### Identity limits

The server assigns an opaque `ParticipantId` when a connection joins the
session. Client slot, display name, IP address and userinfo are attributes, not
identity. Array access still validates the current slot before use.

openQ4 does not yet have a trustworthy account identity. Therefore a
`ParticipantId`, acquired role, ready state, duel-queue position and vote are
connection/session scoped. Disconnect removes them or leaves an explicit vacant
roster seat; a referee may restore the seat manually. Name or IP matching must
not auto-reattach authority. Persistent authenticated reconnect is deferred
until the engine provides a cryptographically sound identity/resume-token
service; the UI and docs state this limitation plainly.

### Capabilities

Roles are convenient capability bundles, not a hidden numeric hierarchy:

- player: self readiness, legal team/queue actions and eligible proposals;
- captain: own-team readiness, lock/roster operations and own-team timeout;
- coach: policy-limited team observation and team communication, no implicit
  readiness or pause power;
- broadcaster: permitted observer projections, no match mutation;
- referee: phase, pause, roster, rules-boundary, veto and proposal moderation;
- server operator/local listen host: server policy, credentials and filesystem
  outputs, reached through trusted local paths.

Capabilities are checked server-side for every operation against the current
phase, target scope and revision. Remote referee authentication, if password
backed initially, is rate-limited, constant-time compared where supported,
connection scoped, cleared on disconnect/map change, never replicated or
logged, and grants only referee capabilities. It never grants console/rcon,
ban-list, filesystem or process access.

### Operation pipeline

An operation request contains a schema version, opcode, expected session
revision, actor connection, typed bounded arguments and optional target
`ParticipantId`/team. Its descriptor declares argument schema, capability,
legal phases, proposal eligibility, cooldown and executor.

All entry points follow one pipeline:

`decode -> structural validation -> resolve principal -> authorize -> check phase/revision -> validate semantics -> commit once -> journal -> publish view`

GUI, console, referee actions and passed proposals call the same executor.
Legacy textual commands may be thin parsers during migration, but must produce
the typed request and cannot invoke arbitrary command text. Operations are
idempotent by revision/result; malformed, unknown, oversized or trailing data
fails closed without partial mutation.

## 8. Rosters, readiness, queues and timeouts

A roster entry names a session participant or a vacant declared seat, side and
role. One pure join evaluator returns allow, queue or deny with a localized
reason after checking phase, capacity, lock, invitation, series roster and mode
policy. Every join path, force-team path and reconnect path uses it.

Readiness policy may be individual, team/captain, both, or disabled. Only
connected active humans assigned to a legal side are eligible; bot handling is
an explicit profile field. A team-ready operation cannot conceal a missing
roster seat. Force-ready records the referee and the blockers overridden.
Roster/rule changes during countdown cancel it; live roster mutation is limited
to explicit substitution policy and is journaled.

Duel uses a FIFO queue of current `ParticipantId`s. Join, defer, leave,
disconnect and promotion are typed operations. A promoted player must ready;
the queue is not rebuilt from client slots or names.

Timeout rules specify per-team count, duration, request window and resume
policy. The session publishes owner, kind, remaining budget, pause reason and
resume deadline. No timeout can be requested by an arbitrary spectator, and a
timeout cannot be charged to both teams or consumed twice.

## 9. Proposals and voting

The two inherited vote transports are replaced by one typed proposal service.
A proposal references an operation descriptor plus validated arguments; it
does not store a command string.

- Scope is global or one team, with at most one active proposal per scope.
- The electorate is a snapshot of eligible human `ParticipantId`s at creation;
  spectators and bots are excluded unless that descriptor explicitly says
  otherwise.
- Required quorum and yes count are computed and frozen at creation. Joins and
  disconnects do not move the threshold or corrupt counters.
- Each electorate member casts at most once. The caller's automatic vote, if
  configured, is explicit in the record.
- Early pass/fail, timeout, cooldown and phase cancellation use descriptor
  policy. The operation and target are revalidated when a proposal passes.
- Kick/team targets are `ParticipantId`s, never unchecked client indices.
- A referee may cancel a malformed/stalled proposal but may not silently turn a
  failed vote into an unrelated operation. Any configured referee override is
  a distinct, audited action.

The proposal view carries localization ids and typed parameters. The server
does not build localized English vote sentences.

## 10. Series, map pools and veto

`mpCompetitionSeries` is optional and sits above map sessions. Its legal states
are `DISABLED`, `SETUP`, `VETO`, `READY`, `MAP_ACTIVE`, `MAP_COMPLETE`,
`SERIES_COMPLETE` and `CANCELLED`.

A series transaction validates an odd best-of count, supported mode, declared
teams, map pool and veto pattern. Every map is installed and supports the mode.
The veto is a descriptor-driven sequence of `BAN`, `PICK` and optional `SIDE`
steps with an expected team at every revision. The initiating side is selected
explicitly by the referee or by a recorded deterministic seed; there is no
unrecorded random choice. Duplicate/out-of-turn choices and exhausted pools are
rejected atomically.

Only a committed map result advances the score. Abort does not; forfeit does.
A selected-map load failure returns the series to `READY`, preserves the last
committed result and records the failure. The schema-4 cross-map recovery record
stores the series core and mutable report draft together. It is bounded,
checksummed, written beneath `fs_savepath` through a fixed server-owned path
using temp-write plus atomic replace, and contains no secret, address or
connection authority. One promotion is the durable commit point for the score,
ordered map result, statistics and evidence/MVD artifact status. Legacy
schema-2 records decode for diagnosis but cannot be resumed because they lack
paired report state. A selected map is not considered bound until the next map
session checkpoints its session/rules identity, preventing a lobby journal from
becoming that map's result. Tournament brackets remain external.

## 11. Spectator policy and recipient views

The server builds `mpSessionView` separately for each recipient. Data is
authorized before serialization; the GUI is never asked to hide a field the
client should not possess.

The view includes public phase, clocks, ready blockers, timeout budgets,
proposal, public roles, series score and the recipient's allowed actions.
Roster invitations, private team state, referee-only reasons, credentials and
operator data are omitted unless authorized. Follow targets, coach access,
team vitals and item timers are filtered by the committed spectator policy.
Managed matches track only supported placed, respawning major items, keyed by
stable map-instance and source identity. Pickup and respawn deadlines use the
pause-safe match clock. Timer projections are available only to explicit
broadcaster and referee recipients, never players, coaches or ordinary
spectators; presentation maps semantic identifiers to localized names and never
renders a raw internal token.

Captain invitations cannot turn a neutral spectator into an authorized live
observer and remain fail-closed. The engine has no implemented, reachable
Q4TV/repeater transport. The pure recipient policy defines a public-only,
no-item-timing result for a possible future repeater adapter, but that defensive
branch is not a shipped live capability. Players cannot gain enemy or timed-item
information by opening a menu, changing follow target, joining late or replaying
a stale packet.

Every view carries schema version, session id and revision. Counts and strings
are bounded, unknown required fields reject the message, and stale revisions do
not roll back client state. The supported broadcaster role and server-owned MVD
do not imply Q4TV, a repeater relay, broadcast delay or live multi-POV. Those
transport features are deferred, and profiles must not label unrestricted
real-time observation as delayed.

## 12. Evidence, stats and MVD

Evidence is produced from committed session events, not scraped console text.
The journal records session/series ids, schema/build version, rules digest,
map/mode, phase and pause transitions, authenticated role changes, proposals,
roster mutations, round/map results and output failures. Final per-map and
series artifacts add bounded per-participant/team stats and result reason.

Server MVD auto-record starts when the rules snapshot freezes at countdown (or
at the earliest reliable transition supported by the existing recorder), rolls
per map, and stops only after the result/review metadata is committed. Manual
recording remains available. Filename components are allowlisted and collision
safe. Recording failure notifies the operator and evidence journal but does not
alter the match result. Playback of the generated MVD is an acceptance test.

Structured output uses an escaping serializer, size/count limits, fixed
savepath-relative directories and atomic finalization. Per-map schema-2 evidence
is optional (`g_matchEvidence` defaults to full journal plus final report), but
an active series always retains the minimum in-memory journal required to seal
an authoritative result. Series completion stores an immutable schema-1 report
before publishing the terminal paired checkpoint. No detached thread owns game
pointers; any worker receives immutable copied data and is joined. Partial
files are recognizable and never replace a valid final report. Reports exclude
IP addresses, passwords and auth material. The server never injects client
screenshots or client demo commands.

## 13. UI, HUD, browser and documentation

There is one localized Match Control surface within the existing multiplayer
menu. It consumes the recipient view and operation descriptors:

- players see readiness, queue/team status, proposals and public match state;
- captains see only legal own-team roster/lock/timeout actions;
- coaches and broadcasters see their observation scope;
- referees see rules staging, phase/pause, roster and veto controls;
- destructive or match-deciding actions require a localized confirmation.

Buttons are enabled from server-published allowed operations and show a typed
localized denial reason after rejection. The UI never predicts authority from
name, client slot or a local cvar.

HUD and scoreboard show the minimum useful context: phase/live period, round,
pause owner/reason/resume countdown, ready blockers, timeout budgets, overtime,
proposal, series map score and relevant public roles. Layout uses generic state
keys/column descriptors rather than gametype-number chains. All visible text is
`#str_*` backed in every openQ4 language table; missing translations may use the
project's documented English fallback but not hardcoded GUI text.

The server browser's game-type registry is rebuilt for all implemented public
modes and supports more than the inherited two-bit/Doom-era filter. Because the
engine chooses a module before loading game code, its public-mode mirror is
generated or mechanically cross-checked against the game descriptor table.
Unknown and hidden modes never bypass a filter. Browser labels, host menu,
votes, map checks and docs use the same public subset.

User documentation is task based: joining/readying, captaining a team,
refereeing a match, operating a server/profile, running a series/veto,
spectating/broadcasting and finding/playing MVD/results. It states role and
reconnect limits and supplies complete working examples without exposing an
implementation-sized command catalogue. Release-completion and curated release
notes are updated as each user-facing slice lands.

## 14. Security and correctness invariants

These are release blockers:

1. The server is authoritative. Client messages carry bounded typed data, never
   console/rcon text or filesystem paths.
2. Legacy remote `SERVER_ADMIN` and `GETADMINBANLIST` requests are rejected.
   Trusted local-host actions use the operation service or a direct local path.
3. Every client slot, team, enum, count, string length and packet remainder is
   validated before indexing, allocating or mutating.
4. Authentication yields explicit scoped capabilities; secrets are neither
   replicated nor logged. Disconnect and map change revoke connection roles.
5. Phase/round/pause transitions occur only through their legal tables and
   commit once. Rules transactions and operation failures leave no partial
   state.
6. Recipient redaction happens before serialization. Stale views cannot reveal
   more than the configured role. The future repeater-policy boundary remains
   public-only and receives no item timing unless a separately designed engine
   transport is implemented and qualified.
7. Proposal electorate/thresholds are immutable, bots/specs are excluded by
   default, and targets are stable participant ids.
8. Each gameplay deadline has exactly one clock/compensation owner. Pause never
   advances gameplay, skips a timeout charge or jumps a trajectory on resume.
9. Optional logging/MVD/UI failure cannot decide or corrupt a match. Output
   paths are server-owned, bounded and atomically finalized.
10. All display text is localized, and all descriptor tables self-validate at
    startup and under contract tests.

## 15. Implementation sequence and acceptance gates

Every phase keeps the tree buildable and preserves unrelated dirty work.
Windows builds use `tools/build/meson_setup.ps1`; canonical game edits remain in
`openQ4-game/src/mpgame`. Runtime checks use the MP launch in windowed mode,
enter a relevant map, read `.home/baseoq4/logs/openq4.log`, and use only the
engine `screenshot` command when visual proof is needed. Tests must not take
control of mouse or keyboard without explicit permission.

### Phase 0 — close current trust and advertisement holes

- Fix ready counting before any team indexing.
- Reject unauthenticated legacy admin/ban-list messages.
- Bounds-check both inherited vote paths, correct packed vote field clearing
  and cover every vote mask bit until migration removes those paths.
- Mark incomplete objective modes non-public and fix module selection, host
  menu, voting and browser filtering accordingly.

Acceptance: adversarial truncated/invalid slot/team packets do not crash or
mutate; spectators cannot administer; public surfaces enumerate exactly the
implemented modes; existing game-type and bot contract tests pass.

### Phase 1 — aggregate, descriptors and rules transactions

- Introduce session revision, legal transition adapters, typed rules drafts and
  immutable snapshots.
- Route existing phase/round changes through the legal transition service.
- Build the recipient view envelope and descriptor self-validation without yet
  adding privileged actions.

Acceptance: table-driven tests cover every legal and illegal phase/round
transition; failed rules commits preserve value/digest/revision; overtime
extends exactly once; all existing supported modes reach gameplay and review.

### Phase 2 — clocks and freeze primitive

- Add clock ownership, pause overlay, central entity/event/deadline freeze and
  resume countdown.
- Convert lifecycle/round limits to match time and audit all absolute
  multiplayer deadlines.

Acceptance: repeatable tests pause mid-projectile, mover, animation, item
respawn, powerup, player weapon cooldown, round and Tourney arena; snapshots and
chat continue; no damage/input occurs; remaining gameplay time and position are
unchanged across pause; multiple pauses add no drift; resume advances once.

### Phase 3 — principals, operations, rosters, readiness and proposals

- Add connection-scoped identity, roles/capabilities and the one operation
  registry/pipeline.
- Route ready/team/queue/admin UI and both managed vote entry points through it.
  Keep inherited vote/admin compatibility only for casual servers and make
  every such mutation fail closed once managed authority is committed.
- Enforce roster/team/spec locks and typed readiness blockers everywhere.

Acceptance: a role/phase/operation permission matrix passes; privilege is lost
on disconnect/map change; every join route agrees; invalid targets never index
userinfo; bots/specs cannot skew votes; fixed thresholds survive disconnect;
the same operation produces the same result from console, GUI or proposal.

### Phase 4 — timeouts and series

- Add team/referee pause operations and budgets on the Phase 2 primitive.
- Add map-pool validation, series lifecycle, deterministic veto and atomic
  recovery record.

Acceptance: simultaneous/duplicate timeout requests charge once; unauthorized
spectators fail; owner/time-in/resume policies work during round and continuous
modes; veto rejects out-of-turn/duplicate/unsupported maps; map-load failure
rolls back safely; abort and forfeit affect series score correctly.

### Phase 5 — views, spectator policy and evidence

- Complete per-recipient projection, follow/coach/broadcast policy and permitted
  item/vital data.
- Integrate stats, audit/final artifacts and automatic server MVD.

Acceptance: byte-level view tests prove forbidden fields are absent for player,
coach, spectator, broadcaster and referee recipients; supported placed-major-
item timers appear only for explicit broadcaster/referee recipients and remain
pause-safe; stale packets cannot widen access. The synthetic future-repeater
policy is tested separately for its public-only, no-timer result without
claiming a live transport. Output survives escaped names and interrupted writes;
no secret/IP appears; generated MVD plays and contains the committed map result.

### Phase 6 — presentation and operator experience

- Build the localized Match Control surface, confirmations, HUD/scoreboard and
  complete browser registry/filter.
- Add task-based user/operator docs, release-completion entry and curated
  release note.

Acceptance: every role can complete its supported workflow without rcon;
disabled actions explain why; every string id exists in all language tables;
all public modes filter correctly; 16:9 and 4:3 windowed engine screenshots
show no clipping; docs commands/profile examples pass smoke tests.

### Phase 7 — full-system hardening

Run static contracts, clean game/client/dedicated builds, parser fuzz cases,
two-human-or-bot network scenarios, reconnect/disconnect/substitution cases,
each supported mode family, repeated map/series transitions and MVD playback.
Resolve relevant warnings in the authoritative log. Performance checks prove
the idle framework allocates no per-frame heap data and recipient views/evidence
are bounded. A final diff audit confirms no `src/game` change and no copied
reference material.

## 16. Disposition of the 2026-07-28 plan

`Keep` means the earlier outcome remains. `Merge` means retain the behaviour but
implement it through this aggregate. `Defer` means a separate track may consume
the framework later. `Reject` means do not implement that design.

| Earlier proposal | Disposition | Authoritative decision |
| --- | --- | --- |
| Match-owned clock and audited pause | Keep | Use the three domains and centralized exactly-once freeze compromise in §5 |
| Independent match-state/authority/dispatch/team/map modules | Merge | One session root, operation registry and optional series; no peer subsystems mutating each other |
| Command descriptor and common validator | Keep | Generalize to typed operations used by every entry point |
| Large cvar and command catalogues | Reject | Minimal compatibility aliases around typed setting/operation descriptors |
| Compiled presets plus executable override `.cfg` | Merge/Reject | Keep named typed profiles; reject privileged arbitrary config execution |
| Loose preset hash/drift polling | Merge | Atomic immutable rules snapshot with canonical digest and staged live edits |
| Referee password and authority mask | Merge | Connection-scoped principal/capabilities, hardened login, no console or ban-list power |
| Separate packed and legacy vote mechanisms | Reject | One proposal service; compatibility parsers only until migration completes |
| Team locks, captains, roster and ready anti-stall | Merge | One roster/join evaluator/readiness gate with explicit blockers |
| Map pool and referee map draft | Merge | Optional series aggregate and validated deterministic veto |
| Seven new reliable message kinds | Reject | Prefer one versioned operation request and one bounded recipient-view stream; reuse existing result/stat channels where safe |
| Delta-tagged replication idiom | Keep | Apply to the bounded session view with schema and revision |
| Scoreboard column model and role-aware HUD | Keep | Driven by descriptors/session view, not gametype integer branches |
| Stats, live accuracy, final report and audit log | Merge | Evidence service over the existing stat manager; safe serializer and atomic artifacts |
| Damage-number slice already landed | Keep | Preserve as optional cosmetic feedback; it is not match authority or evidence |
| Item timers and spectator tools | Merge | Track supported placed respawning major items on the pause-safe match clock; project only to explicit broadcaster/referee recipients and never render raw tokens |
| Autoaction screenshots/client commands | Reject | Server MVD and structured artifacts; client capture remains local opt-in |
| Manual/demo discussion predating current MVD | Replace | Integrate the existing server MVD recorder with lifecycle and evidence |
| Cosmetic parity/forced models/FOV/simple items | Defer | Separate fair-play/settings track unless a committed rule directly needs one bounded field |
| Chat/command flood protection | Merge | Bounded operation/auth rate limits here; general chat moderation remains separate |
| Inactivity, self-kill policy and chat location macros | Defer | Gameplay/moderation features, not match-management foundations |
| Live multiview/Q4TV/broadcast delay | Defer | Broadcaster role and server MVD now; the pure public-only repeater policy is only a future boundary, while engine repeater transport, true delay and live multi-POV require a separate design |
| Persistent roster identity/reconnect by token/name/IP | Reject/Defer | No fake identity; connection scope now, authenticated identity service later |
| Lag compensation/network cvar enforcement | Defer | Dedicated netcode track with remote-client validation |
| Fixed localization-id band and hand-counted tables | Merge | Keep complete localization and validation; allocate ids through current project policy, not speculative catalogues |

## 17. Definition of done

The framework is complete only when every supported mode uses the same
authoritative session, every mutation reaches the typed operation/outcome path,
pause invariants and security tests pass, hidden modes are absent from every
public surface, role-filtered views contain no forbidden data, a full series can
be run from localized UI without rcon, its MVD and results replay/read correctly,
client and dedicated builds pass, relevant MP runtime logs are clean, and user
plus release documentation describes what actually shipped.
