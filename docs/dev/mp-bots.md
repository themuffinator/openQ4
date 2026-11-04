# Multiplayer bots

openQ4 multiplayer generates bot navigation at map load without a per-map
authoring or compile step. Bots arrive with a name, a way of playing and
something to say about it. This describes their behaviour, validation and
current limits.

## Exact armour and timer qualification, 4 November 2026

The causal harness previously restored health and knockback but left armour at
spawn defaults. Earlier impulse exclusions below are historical assessments,
not final MP-02 qualification: missing armour could make a recoverable landing
appear fatal. Policy version 2 requires logged post-hit armour and armour saved
by the hit, restores both post-hit and counterfactual inventories, and fails
closed for missing or out-of-range state. Same-frame shotgun grouping also
requires armour continuity. Raw reports and earlier assessments remain intact.

`bot_probeHealth client health [knockbackMsec [kickX kickY kickZ [armor]]]`
restores an optional exact armour value. `BOT_IMPULSE` records `armor` after the
hit and `armorSave`. It also records the actual knockback interval after
`SetKnockBack` and `preKnock`: an existing movement timer can refuse a hit's
requested interval. Counterfactuals preserve the earlier knock, and shotgun
groups preserve the first pellet's prior timer. Probes replace inherited timers
explicitly, including zero, so a neutral setup's landing cannot silently refuse
the recorded interval. Older logs cannot qualify version-2 exclusions.

The `a3172f89a` pad-catch correction passes both recorded DM8 hit poses and the
CTF5 diagonal-pad hit replay, all alive and grounded at the strict terminal
check. The DM8 rocket slide remains recoverable by fixed steering but its
controller fails. Ground-slide traces reveal a one-frame timer mismatch:
player physics drops knockback timers before movement, while prediction waited
until the next frame to restore friction and ground acceleration. The predictor
now uses the same expiration frame. Synthetic rim tests compare the expiring
timer with an extra knockback frame. On `0d587c92a` the controller recovers to
the upper platform and ends alive/grounded at 67 health; the fixed southwest
control still survives at 62 health. Evidence is `rocket-armor-timer/`.

The `c65041464` catalog completes all 33 variants (at least 47.376 observed
seconds), with 448 combat kills, 618 traversals and four raw world deaths.
Besides the corrected CTF5 pad catch, it exposes a low-health CTF7 drop after
recovery from a rail hit. Both isolated drop poses reproduce the three-damage
fatal fall at three health and 100 armour (`ctf7drop-armor-timer/`). The route
targets the raised floor at -111.75, but momentum takes the bot onto the lower
neighbour near -127.75. That 384-unit fall exceeds its hard-fall budget. The
general edge guard previously accepted any supported floor within its fixed
depth and therefore skipped the live drop proof. It now also prices the actual
sampled floor with current health and armour. The existing full/careful drop
proof may then earn a safe departure; otherwise the bot brakes. Native controls
retain affordable floors, armour effects and disabled falling damage without
waiving voids. On `dc89724d1`, both drop poses land on the raised floor with two
health and finish alive/grounded (the later pose collects health afterwards).
Evidence is `ctf7drop-floor/`; `hit-floor/` expands the positive suite to eight
cases. Final qualification will use the corrected timer-replay implementation.
Final-build long matches, catalog and controlled timing remain open.

The `9a70f7b68` stage passes all eight hit/drop recovery cases and all ten
established air-recovery outcomes (`hit-state/`, `air-state/`), with unchanged
runtime hashes. Windows and Linux ASan/UBSan fixtures reject 45 restored bugs:
22 decision, 19 geometry/timer and four hazard reversions. Policy and navigation/
combat source checks pass. The renewed `long-state/` sweep completes all four
maps. CTF3 finishes with 270.272 observed seconds, 33 kills and zero world deaths;
DeadZone records 281.552 seconds, 65 kills and no world deaths. CTF5 retains
seven world deaths in 280.048 seconds, with 79 kills; all seven pass the exact-state
causal exclusion.
DM8's raw report fails with eight world deaths and only 238.368 observed seconds
against the required 270, with 80 kills. Seven deaths pass the causal exclusion;
one remains recoverable. Duration remains an independent failure regardless
of those outcomes. Shared-host timing is not
qualified (DM8 mean think 2.747 ms; worst-window p95 28.75 ms).
Together the matches record 505 completed traversals, four stuck events and no
traversal timeouts, goal give-ups or no-route events. The `long-state/assessment.json`
therefore remains failed: 15 raw deaths, 14 excluded impulses and one navigation
death. It rechecks each stored exclusion against the current policy, including
the requirement that the controller trial itself armed and died. A missing or
unresolved controller now fails closed, even if all fixed-input trials died.

The identical-stage `catalog-state/` sweep finishes all 33 variants, with 421
kills, 619 completed traversals and two CTF5 world deaths. Both pass the exact-state
causal exclusion; no navigation deaths remain in that sweep. Four runs miss the minimum
40 simulated seconds: DM11 (37.296), DM10 (32.880), CTF8 (25.520) and Tourney1
(22.768). Those raw duration failures are retained; shared-host contention does
not waive the required exposure.

The remaining DM8 death is Strauss's rocket hit at 115744, with 12 health and
no armour. `strauss-state/` reproduces the controller's fatal slide after touching
the raised ledge, while west-northwest input stops alive at 12 health.
`strauss-floor/` confirms that this landing rests on the hull edge and lacks
broad route support. Recovery now considers a separate flat-floor braking
continuation: complete the collision frame, retain a shared floor patch and
actual contacts through braking, and reject slippery floors, hazards, transport
volumes and gaps. It prefers that surviving stop over an unproven first touch;
voluntary traversal support remains unchanged. A native geometry case separates
the two headings, and the positive hit suite includes the recorded state.
Runtime qualification of this further correction remains open.

On `189c4b4a9`, `strauss-runout/` records the controller stopping at
68.50 -1037.88 352.25 with 12 health, then completing a drop, teleport and jump
without dying. Its six-second strict terminal check remains unresolved because
normal navigation has put it in the air again (59 health). The fixed heading
still finishes alive and grounded at 12 health. The positive recovery case uses
the existing 12-second/resumed-navigation check; the six-second causal gate and
raw result are unchanged. The geometry suite rejects 23 reversions on Windows
and Linux ASan/UBSan, four more than the earlier stage. `hit-runout/` passes all
nine positive hit/drop cases and `air-runout/` passes all ten established outcomes,
including the deliberate fatal-fall control. The Strauss positive case remains
alive at 59 health and qualifies by completed navigation after recovery; its
strict terminal-grounded result is still false. All tested runtime hashes remain
unchanged. The renewed `long-runout/` sweep records 340.560–342.240 simulated
seconds on each map, 337 combat kills and 633 completed traversals. DeadZone has
zero world deaths; DM8 has five and CTF5 six under causal review. CTF3 has two
navigation deaths: every tested input survives Kane's last recorded hit, while
Cortez has no recent impulse. The original duration failure is resolved on this
stage, but the navigation gate remains failed. Its complete initial causal
assessment (`long-runout/assessment.json`) excludes eight of 13 deaths and
retains five. An additional CTF5 counterfactual restores the recorded walking
corner at the same pre-hit pose, health, armour and timer on the archived
`189c4b4a9` runtime. It finishes alive and grounded at six seconds, while all
17 fixed post-hit inputs and the controller died. The existing policy therefore
excludes that rocket death (`ctf5-runout-route-counterfactual/`);
`long-runout/assessment-route.json` retains four navigation deaths. The original
assessment is preserved. This is an additional causal proof, not a bot change.

All three `ctf3pad-runout/` isolated launch/coast controls land alive, including
Cortez's rounded launch pose. The original match instead launches Strauss from
the same pad 192 ms later. `bot_probePad` now accepts optional `entryX entryY entryZ`
after `padName` to reproduce a walking approach. `bot_probePadTraffic` uses the
same arguments but requires exactly two bots and a separate local observer;
its bots become untargetable so weapon impulses cannot obscure physical traffic.
Both retain local cheat-server, live-bot, instance and proven-pad restrictions.
At debug level 3 or higher, `BOT_PLAYER_CONTACT` records player collision traces
without changing their physics. These diagnostics support the next reproduction;
they do not classify a death as unavoidable.

The two-bot reconstruction on `893fe4f13` reproduces that fall at
3248 3220 -461 (`pad-traffic-diagnostic/`). The older approach is making a slow
climb hop when the follower's grounded-only reservation check stops seeing it.
The follower launches first, collides twice with the hopping bot and loses
horizontal velocity; the first bot launches 176 ms later and completes the pad.
Fresh approaches now retain their reservation while airborne, using the existing
stale timeout and route release. The native decision case fails before this
change and passes afterwards; restoring the grounded-only check fails again.
`mp_bot_pad_traffic_replay.py` preserves the two-bot reproduction as a positive
runtime regression. On `04e637ae0`, both bots complete the pad in priority order,
without colliding during its approach/flight, and remain alive throughout the
12-second observation (`traffic-hop-staged/`). Ordinary contacts after both
flights finish remain reported. The first draft of that harness wrongly rejected
those later contacts; its failed `traffic-hop/` report is preserved. The unfixed
`traffic-before/` still fails the corrected check with five pad contacts and a
world death. All 23 decision reversal controls pass on Windows and Linux
ASan/UBSan; the geometry and hazard functions are unchanged from `189c4b4a9`.
Kane's other CTF3 departure remains unresolved; the initial isolated walking
approach survives and is not evidence that the original match was safe.
The later sampled grounded pose and an early-hop reconstruction also survive
(`pad-kane-entry-entry/`, `pad-kane-hop-hop/`). Both remaining DM8 hit poses,
including the rocket's first recovery frame, survive 12-second isolated checks
on this stage (`departures-dm8-hop64/`). Their original full-match departures
remain unisolated. A diagnostic script that exceeded the engine's 64 KiB command
buffer is retained as failed evidence; the replay launcher now rejects oversized
scripts before starting a game, without shortening their observation windows.

The `04e637ae0` staged `long-hop/` repeat passes CTF3 directly: 386.640 observed
seconds, 58 kills and 116 completed traversals, with zero world deaths. A separate
CTF3 diagnostic run records 298.896 seconds, 47 kills and 91 traversals with zero
deaths. DM8 records 369.744 seconds, 139 kills, 169 traversals and six deaths.
All six pass the unchanged causal policy: Sledge's pre-hit controller needs an
additional 12-second observation to finish grounded, at 100 health. Its earlier
six-second terminal-air result remains unresolved and preserved. CTF5 has seven
deaths in 192.752 seconds; DeadZone has no
deaths in 253.216 seconds. Both miss the required 270 seconds and remain failed.
The complete initial assessment excludes 11 of 13 deaths and retains two CTF5
navigation failures (`long-hop/assessment.json`). One is a recoverable rocket
hit whose chosen heading glances off a ramp and leaves the walkway; its
controller also dies in the isolated `current-failures-hop/` reconstruction.
The other follows an unplanned pad-chain entry at nine health. Two isolated
walking approaches stop safely, so its full-match entry remains unresolved.

The same stage passes all nine positive hit/drop cases and ten established
air-recovery outcomes (`hit-hop/`, `air-hop/`). A stationary player hull near the
recorded DM8 rocket trajectory reproduces its older fall (`body-hop-static/`):
the diagonal body collision removes the safe coasting trajectory. The identical
hit without that body survives. This establishes a physical-traffic failure
mode, not an exact replay of the original moving player or a causal exclusion.
The later machine-gun fall still survives the stationary-body reconstruction.

The audit now optionally extends its observation until the logged server clock
meets `--min-sim-seconds`, within the existing hard timeout. Missing logs, partial
lines and clock resets cannot supply elapsed simulation time. The default fixed
window and final duration/death criteria remain unchanged; Windows/Linux clock
controls and a stock-map pilot pass. `long-hop-duration/` repeats CTF5 and
DeadZone for 277.984 and 278.080 observed seconds, respectively. CTF5 records
82 kills, 188 traversals and five deaths; four pass causal exclusion. Kane's
remaining case fails closed because fixed input `air04` is still alive and
airborne at six seconds, while the other post-hit inputs and controller die.
DeadZone records 55 kills, 78 traversals and no world deaths. The combined
same-runtime four-map assessment records 334 kills, 551 traversals, 11 raw
deaths, ten exclusions and that unresolved case (`long-hop-combined/`). It does
not erase the earlier short runs or the two CTF5 failures in `long-hop/`.

The complete `catalog-hop/` sweep reaches all 33 variants with at least 97.216
observed seconds each, 915 kills and 1,283 traversals. DM8's one death and CTF5's
two pass strict causal exclusion. DM6's one death after a shotgun hit still
needs replay, so `catalog-hop/assessment.json` remains failed. The sweep also
retains 125 stuck events, five goal give-ups and one traversal timeout (DM6).
Remaining failure fixes and controlled timing are still required. Shared-host
frame-trace runs do not establish performance budgets.

The CTF5 ramp reconstruction records a world collision at
`539.182 777.780 -180.667`, normal `0 0.519 0.855`. Player physics redirects
the descending bot upward; its hull then loses contact. The predictor had
required end-of-step grounding only when the apparent impact exceeded the
health budget. It now completes the clipped collision frame for affordable
touches too, continuing the flight when actual ground contacts are absent.
The new native case fails before this change and passes afterwards, with all
24 geometry reversions rejected on Windows and Linux ASan/UBSan. The recorded
hit joins the positive suite as `rocket_ramp_glance`. On the `5a2f78a3d` candidate,
the controller survives twelve seconds and finishes grounded at 100 health
(`current-failures-ground/`), where the prior stage died.

The outstanding `04e637ae0` classifications are now complete. Extending Kane's
identical `air04` input to twelve seconds records its world death; its apparent
six-second survival was still a fall. The unchanged policy therefore excludes
that impulse (`ctf5-hop-air04-extended/`). The derived four-map assessment
`long-hop-combined-extended/` passes with 334 kills, 551 traversals and eleven
strict exclusions. The DM6 shotgun death also passes exact-state replay,
so `catalog-hop/assessment-dm6.json` passes all 33 death/duration gates with
915 kills, 1,283 traversals and four exclusions. Earlier failed assessments,
short runs and the two separate CTF5 failures remain preserved. These results
qualify the archived stage, not the subsequent corrections or timing.

### Live recovery around pads and players

The remaining CTF5 unplanned entry was preceded by an exhausted route. Its
initial no-route guard checked momentum before recovery chose a new command;
nearest-node recovery and wandering then applied only the edge guard. The
normal WALK probes therefore exercised the wrong movement path. The new local
cheat `bot_probeRecover` supplies a bounded nearest-node answer while running
normal off-route movement and physics. Two westward reconstructions at nine
health enter the unplanned pad and die (`unplanned-pad-before/`). Applying the
transport guard after recovery's final command, including a supported-rim
crawl, keeps all four nearby approaches alive and grounded at nine health for
twelve seconds (`unplanned-pad-guarded/`). The node targets are diagnostic
reconstructions, not an exact snapshot of the original match. Native tests
reject the old node and wander paths; all 25 decision reversions pass on
Windows and Linux ASan/UBSan.

The DM8 stationary-body envelope has three surviving headings among sixteen,
while its controller and no-input trial die (`body-hop-envelope/`). Navmesh
generation correctly excludes players from architecture, but live airborne
recovery reused that exclusion and forecast a coast through another player.
Recovery now additionally sweeps the current live hulls in the same instance,
excluding itself, spectators, dead players and non-solid clips. World collision
and the existing affordable-floor proof still apply. This is a current-position
forecast refreshed by replanning, not a prediction of another player's future
commands. The rocket collision now survives at 66 health, and the later falling
body-contact probe at 31 health, both grounded after twelve seconds
(`body-predicted/`); `body-ground/` preserves the prior controller failure.
Both become positive regressions alongside the four off-route cases. The
original moving-player departures and Kane's older CTF3 missed-pad departure
are not claimed as exact reproductions. Final-build long matches, all 33 map
variants and controlled timing remain required.

### Wall-arrested landings and settled braking, 4 November 2026

The `5bd8dff11` four-map repeat meets every duration gate (274.560–275.888
simulated seconds), with 273 kills, 515 traversals and eleven raw world deaths.
Exact-state replay leaves two unresolved deaths: Kane's CTF5 wall landing and
a DM8 enemy-rocket/self-splash sequence. The 33-variant catalog retains 439
kills, 602 traversals and three raw deaths, all classified as enemy impulses.
The reports remain in `.tmp/mp02-falls/long-guard-standard/` and
`catalog-guard/`; `assessment-ordered.json` preserves the derived assessments.
These are historical stage results, not qualification of the following fix.

Equal-strength machine-gun hits were ordered by rounding noise in their logged
vectors, sometimes choosing an older surviving pose before the actual fatal
hit. Earlier candidates now compare magnitude to two decimals and then recency.
Stronger hits still come first; only a complete all-dead envelope advances the
search, and any survivor or incomplete trial still stops it. The bounded search
and policy-2 causal requirements are unchanged.

The CTF5 controller rejected a coast whose landing runout was safely arrested
by a wall, then braked in air and fell short of the walkway. Emergency recovery
now admits one vertical wall collision with supported movement on both sides
of the contact and a clear, grounded clipped remainder. A corner, lost support,
hazard or unplanned transport still rejects it. This does not admit voluntary
routes. The recorded pose now reaches the walkway alive at 45 health and stops
before normal navigation resumes (`guard-failure-coast-wall-brake/`).

The fixed-input replay also exposed a separate ground brake oscillation:
full reverse acceleration alternated between 14 and -72.4 units/second and
walked an initially safe landing off the ledge after ten seconds. Both replay
and production recovery now scale the final brake to the remaining speed after
friction. Production runout continues to ten units/second, matching the proof's
final friction-only coast. Native controls integrate the actual generated
commands at several speeds and view angles, rejecting reversal and cumulative
drift; wall controls retain a slide-off-the-wall negative case.

The positive hit suite adds the recorded controller and fixed-coast poses,
both observed for twelve seconds. The controller must already be alive,
grounded and stopped before its diagnostic input is released at two seconds,
then remain alive and grounded. Causal replay never uses this release. Earlier
fixed-input deaths may include the old brake artifact, so fresh long-match and
complete-catalog runs on the corrected stage are required. The DM8 duel's
original firing decision is not recorded; its self-splash is neither waived
as an enemy impulse nor claimed fixed from the isolated post-hit failure.

The corrected stop also changes subsequent roaming in the existing DM8 player
deflection case. `hit-wall/` preserves its strict terminal-air result: it first
lands alive, then completes a pad, jump and drop without dying. This positive
case now uses the established requirement of at least two completed traversals
after recovery when normal navigation has resumed. The original strict outcome
remains in every report; death-exclusion criteria are unaffected.

The committed `51517896f` build passes the 27 decision and 24 geometry reversal
controls on Windows and Linux ASan/UBSan, plus navigation, combat, causal-policy,
audit-clock and repository-path checks. All eighteen hit cases, ten recovery
cases and the two-bot pad-traffic check pass on that same committed stage
(`hit-wall-final/`, `air-wall-final/`, `traffic-wall-final/`). Both new wall cases finish grounded at
45 health after twelve seconds, including rest before controller release.
`guard-failure-selfblast-wall-envelope-sampled/` tests all sixteen fixed headings
and no input for twelve seconds on the committed stage; all seventeen die.
The same source's controller and brake also die in
`guard-failure-selfblast-wall-settle/`. This establishes no surviving tested
post-blast input, not a causal exclusion: the bot's already-fired projectile
is not recreated in the enemy-hit counterfactual. The final runtime is preserved
by `runtime-wall.json` and its hash-checked delta archive.

The complete `catalog-wall/assessment.json` passes all 33 variants with 471
kills, 616 traversals and two strict impulse exclusions. `long-wall/assessment.json`
meets all four duration gates (276.720–284.560 simulated seconds), with 293
kills, 469 traversals, four exclusions and one recoverable CTF5 death. Sledge's
nine-health descent at 118512 ms has five surviving fixed headings, while its
controller dies. `guard-failure-pad-descent-wall-diagnostic/` reproduces that
failure and a surviving westward input. The forecast rejects every steered
approach to the catch pad, then chooses a coast whose landing costs too much
health. The next correction forecasts the adjustment before the catch followed
by the normal coast that owns the bot's movement after launch. Its whole
continuation must still be affordable and supported; disabled/unknown pads,
teleporters, initial-pad poses and a second distinct pad remain unproven.
Native controls require the approach and reject continuing that input after
launch. All 26 geometry reversions pass on Windows and Linux ASan/UBSan.
`guard-failure-pad-descent-pad-catch/` now lands its controller alive at four
health, then completes a further pad and drop without dying. The terminal
sample is airborne in another route, and remains a strict terminal failure;
the new positive regression uses the existing two-completed-traversals
criterion. The fixed westward control remains grounded at four health.
Renewed full-runtime qualification of this further correction is required.

The verified fixes through `51517896f` are merged into the main checkout by
`79df4b6bc`; its integration build and staging pass. That merge does not close
MP-02 or qualify the subsequent pad-approach correction.

### Pending climbing hops after recovery, 4 November 2026

The `26f2bcd64` stage passes all nineteen positive hit cases, ten recovery
cases and the two-bot pad-traffic check (`hit-pad/`, `air-pad/`, `traffic-pad/`).
`catalog-pad/assessment.json` qualifies all 33 variants with 463 kills, 627
traversals and one strictly excluded enemy impulse. `long-pad/assessment.json`
meets all four duration gates (276.048–279.088 simulated seconds), with 285
kills, 520 traversals, seven strict exclusions and one CTF5 navigation death.
Runtime hashes and policy-2 parentage are checked by both derived assessments;
the raw failures remain unchanged. The stage is preserved by `runtime-pad.json`
and its delta archive. The source is merged into the main checkout by
`1417cda59`; its build, staging and ten CTF5 positive replays pass.

Rhodes's CTF5 rail hit at 116448 ms is recoverable in four fixed directions.
Its isolated controller survives but ends airborne after picking up health,
so it cannot exclude the original death at 121072 ms. The original bot lands
at 117184 ms, then resumes its route. At 118272 ms a pending climb attempt
jumps toward the western corner while carrying roughly 350 units/second
eastward; the ensuing fall kills it without another impulse. Ordinary climbing
hops bypass the momentum-aware proof and held input used by dodge hops.
`bot_probeClimb` restores that pending window on an isolated local cheat server
without forcing its jump input. The recorded pre-touch pose at 118256 ms
reproduces the fall when the fresh jump pulse arrives before grounded braking
(`guard-failure-climb-baseline-edge/`). The opposite input phase first brakes,
then survives; both outcomes and the diagnostic-only runtime are preserved.
These are isolated reconstructions, not full match snapshots or exclusions.
Ordinary climbing attempts now use the existing momentum-aware dodge proof
and hold the admitted input through the flight. Planned volume-entry hops keep
their transport controller. `guard-failure-climb-fixed/` refuses the unsafe
launch, brakes on the walkway and finishes alive and grounded after twelve
seconds, as does its ordinary-walk control. The 27 production decision
reversions pass on Windows and Linux ASan/UBSan. Both input phases enter the
positive hit suite; full runtime qualification and controlled timing remain
open.

The committed `f57364857` stage passes all 21 positive hit cases, ten recovery
cases and pad traffic. `long-climb/assessment.json` passes all four long-match
death/duration gates (275.056–286.144 simulated seconds), with 279 kills, 516
completed traversals and six policy-2 enemy-impulse exclusions. The complete
`catalog-climb/assessment.json` retains one CTF3 navigation death after two
strict exclusions: 481 kills and 616 traversals across all 33 variants. The
runtime is archived by `runtime-climb.json`. The source is merged into the main
checkout at `4768b7d61`, whose build, staging and twelve CTF5 positive cases pass.

### Unpressed ramp flights and consumed walking routes, 4 November 2026

The CTF3 catalog's Cortez death at 35056 ms remains a navigation failure.
Its earlier machine-gun hit has surviving counterfactual inputs. Later, the
bot runs beside `func_jumppad_10`, rides its sloping rim upward and misses the
launcher. The sampled airborne pose at 34240 ms is already too late for the
tested recovery inputs (`guard-failure-missed-pad-baseline/`). Two reconstructed
grounded approaches with an explicitly planned pad launch survive, while an
ordinary walking leg from the same later ground state reproduces a fatal ramp
departure (`guard-failure-missed-entry-baseline/`).

In that walking reconstruction the first airborne frame still rises at 112
units/second. The follower consumes its nearby final corner and clears the
route. Recovery previously required a descending flight with a retained WALK
corner, so both ascent and route completion prevented intervention. An unlaunched
pad in the next corner also suppressed it. `guard-failure-ramp-launch-baseline/`
replays the first airborne pose: ordinary walking and a fixed eastward input
die, while forced recovery lands at 25 health and completes a pad and a jump.
That control finishes alive but airborne in a later route; its strict terminal
failure is retained, and it is not a causal exclusion.

Recovery now admits an unsafe unpressed flight while rising, after its walking
route is consumed, or while still missing an intended pad. A safe coast,
voluntary jump, planned drop/teleport entry and actual pad volume or flight keep
their previous ownership. Thirty production-decision reversal controls pass on
Windows and Linux ASan/UBSan, including separate reversions of each admission
change. Three positive CTF3 cases cover the grounded walking approach, its first
airborne pose and an unlaunched pad approach. Runtime qualification continues.

The initial pad replay was invalid: its health command had an extra argument,
and settling the saved airborne pose on the ramp left an artificial grounded
physics flag. Positive-only preparation now primes an airborne state before
the measured trial, then restores the recorded pose and health. Preparing it
inside the trial instead produced two probe markers and correctly failed
arming. Both failed reports remain in `hit-ramp-candidate/` and
`hit-ramp-airborne/`. The valid pad replay in `hit-ramp-primed/` survives;
that run's ordinary airborne walk recovers but ends during its next route
before a second traversal completes, so its failed twelve-second result is
retained and the observation is extended to eighteen seconds. No death
exclusion uses this positive-only preparation or resumed-navigation criterion.
Replay assessment now rejects probe usage, precondition and unknown-command
errors even when a later movement probe arms. Negative controls cover both
causal survival and resumed-navigation assessment.

`hit-ramp-extended/report.json` passes all four CTF3 cases on the candidate
stage: each ramp case lands alive and completes two further traversals, with
74–99 health at the end. Final Windows and Linux ASan/UBSan decision suites
pass all thirty reversal controls. The committed build still requires the
complete long-match, catalog and controlled-timing gates.

The committed `f6aa481e6` runtime is preserved in `runtime-ramp.json` and
`runtime-ramp.zip`. Its 24 hit cases pass `hit-ramp/assessment.json`, which
records the hashes of both source reports. The original full run passed 23/24:
the pad approach recovered, then ended airborne during its next jump after
one completed traversal. `hit-ramp-ctf3-extended/` keeps identical setup and
commands, extends that observation to eighteen seconds and passes all four
CTF3 cases. The original failed report remains unchanged. The ten established
air-recovery cases and two-bot pad traffic also pass on this exact stage.

`catalog-ramp/assessment.json` passes every declared variant's death and
duration gate: 33 maps, 424 kills and 605 completed traversals. Its single raw
CTF5 rail-hit death passes policy-2 exclusion, leaving zero navigation deaths.
The raw report remains a failure before that assessment. Three no-route
reports, 49 stalls and one goal give-up remain recorded, with no traversal
timeouts. This is safety/duration evidence, not controlled timing.

The fix and observation update are landed in the main checkout at `26460c802`.
Its build/stage and all four CTF3 recovery cases pass in
`E:/Repositories/openQ4/.tmp/mp02-integration/hit-ramp-ctf3/`. An initial local
build attempt was stopped during reconfiguration after detecting another
task's active build in the same directory; the subsequent non-overlapping
build and staging both succeeded. The main checkout contains additional
integrations, so this package check supplements the archived exact-stage
safety evidence rather than changing its recorded runtime identity.

`long-ramp/assessment.json` also passes all four long-match death/duration
gates on `f6aa481e6`, with 275.664–279.008 simulated seconds per map, 286 combat
kills and 498 completed traversals. CTF3 and DeadZone have no raw world deaths.
DM8's two and CTF5's five pass the unchanged exact-state policy-2 exclusion:
every post-hit steering/no-input/controller trial dies, and a corresponding
no-hit control survives. Three CTF5 bullet hits arrived after the fall was
already lost; their complete failed counterfactuals required tracing back to
the earlier rail hit before exclusion. All original reports and each attempted
cause remain preserved. The four matches retain ten stalls, with zero no-route
reports, goal give-ups or traversal timeouts. Controlled timing remains a
separate gate; these shared-host functional runs do not qualify it.

The subsequent five-minute CTF3 timing attempt (`timing-ramp/report.json`)
meets the selected numerical limits: **0.791 ms** weighted mean think against
4 ms, **4.25 ms** worst-window p95 against 8 ms, and **744 ms** navigation
generation against 1,000 ms. Six skill-4 bots play 284.256 simulated seconds,
with 41 kills, 103 traversals and a 3–2 capture score, with zero world deaths,
stalls, no-route reports, goal give-ups or traversal timeouts. Runtime hashes
remain unchanged. Host/build/limit metadata is in `timing-ramp-host.json`.

**Controlled timing is still unqualified.** The mandatory workload monitor
observes two separate SP processes from the `9514` checkout (PIDs 7192 and
40324); one overlaps the entire run. Contention is the report's only failure.
Those processes belong to another task and were left running. No uncontended
repeat was completed. On 4 November 2026 the project owner accepted this
timing-contention caveat and directed MP-02's roadmap closeout on the passing
safety evidence above. The raw timing report retains its failure, and the
contention monitor and numerical thresholds remain unchanged. Controlled
timing qualification would still require a quiet-host repeat; the recorded
measurements cover six skill-4 bots on CTF3, Windows x64/OpenGL and the
`f6aa481e6` debugoptimized runtime on an i7-13700H. They do not qualify other
maps/platforms. Earlier open-MP-02 statements in this investigation record
describe historical stages, superseded by this accepted closeout.

Evidence preservation, 4 November 2026: before removing the merged `04c0`
worktree, its useful local evidence was archived in the main checkout at
`.tmp/worktree-evidence/mp02-04c0-2026-11-04/evidence.zip`. The archive retains
10,906 files, including original failed reports, replay/configuration data,
build records and all 24 files of the qualified runtime. Every archived file
was read back and matched against its source SHA-256; `manifest.json` records
the paths and hashes, and `verification.json` records the successful check.
The archive SHA-256 is
`bd7202aa49516c1e306ecffeca730d182f905154188e265d27df73c9492f048f`.
Generated runtime caches and disposable build/dependency outputs are excluded
and inventoried separately. Extract the archive into a project-local evidence
directory to restore its worktree-relative paths; absolute paths inside the
original reports remain historical provenance.

An unrelated Vulkan shadow-resource fallback warning was observed during
earlier diagnostics. The final safety sweeps and this timing attempt use
OpenGL and do not establish Vulkan rendering qualification.

## Hit-to-movement handoff, 4 November 2026

The three remaining long-match falls expose two timing gaps. DM8 Strauss's
machine-gun hit leaves two health during a committed jump; the 35-unit kick
was below the recovery threshold, and its old lower landing was now lethal.
CTF3 Sledge's roughly 130-unit nail impulse also missed that threshold; by
the first descending WALK recovery, the reachable ledge had been lost.
CTF5 Strauss's rail hit lands after bot command generation but before player
physics. One more frame of the pad command advances it beyond the recoverable
hit pose. Replays from the next logged pose reproduce that failure; debug
command traces show full movement speed and later oscillating fallback input,
not a speed or view-angle mismatch.

Every nonzero kick now invalidates the recovery plan. A hit during a jump or
drop protects an affordable coast rather than continuing an obsolete takeoff
proof. Player Think also refreshes recovery movement after copying its command,
so an earlier entity's hit can change this frame's movement before physics.
Safe pad flights retain their controller. The refresh leaves view and weapon
input intact, and its CPU cost joins the same frame's bot profiling sample.

`bot_probeHealth client health [knockbackMsec [kickX kickY kickZ]]` can now
replay the real hit handler. The optional kick is already included in the
following probe's velocity; it is never added twice. The isolated local cheat
server restrictions remain. `mp_bot_hit_recovery_replay.py` exercises the three
recorded states, including natural walking and the committed jump; these are
positive recovery checks, not complete match snapshots or causal exclusions.
Native decision tests reject restored weak-hit thresholds, delayed command
refresh and continued jump ownership. Runtime qualification is in progress
under `.tmp/mp02-falls/`; the prior failed reports remain unchanged.

On `84dde9c6d`, all three hit replays and the ten existing recovery cases pass.
The DM8 and CTF5 hit cases use a 12-second positive observation because normal
navigation resumes after recovery; both finish alive/grounded and complete two
subsequent traversals. Their original six-second reports remain failed terminal
checks. Causal envelopes still use six seconds and their unchanged strict
terminal-state rule. Windows and Linux ASan/UBSan decision, geometry and hazard
fixtures reject 31 reversions.

The four renewed long runs retain 283.872–284.912 seconds of simulation each,
with 104/48/78/69 combat kills on DM8/CTF3/CTF5/DZ3. CTF3 and DZ3 have no world
deaths. Both CTF5 deaths pass the strict enemy-impulse exclusion. DM8 has five
world deaths, three excluded and two still recoverable: a self-rocket after a
high ledge jump and a low-health pad descent. Reports and classifications are
`long-final/`, `long-final-classification/` and `long-ctf5-classification/`.

The next correction refuses exposed self-blasts during an airborne flight,
while retaining distant splash shots. It also preserves descent in the splash
landing predictor until actual floor contact. The low-health DM8 descent reveals
a separate collision distinction: a bevel sweep clips downward velocity before
the player registers ground contact. CrashLand therefore does not charge the
first collision's impact. Recovery now follows the remaining single-plane step
with the player's own velocity-clipping function and checks actual end-of-step
contacts before deferring impact cost; ordinary grounded impacts retain their
health limit. Targeted and broader runtime qualification of these corrections
is still required.

The subsequent `c65041464` stage passes the four hit replays (including the
seven-health descent) and all ten established recovery cases. Each new hit case
finishes alive and grounded. The contact proof and airborne self-blast controls
bring the Windows/Linux native suite to 36 rejected reversions. The renewed
long runs finish with 280.144–283.984 observed seconds: CTF3 and DZ3 have no
world deaths, and all three CTF5 deaths pass the unchanged impulse exclusion.
DM8 has four deaths: two strictly excluded impulses and two recoverable failures.
One rocket-driven ground slide needs earlier steering. Its first machine-gun hit in
Voss's last fall independently reproduces a controller failure: coasting from
`(-444.388, 53.313, 982.004)` catches another pad and lands alive on the upper
platform, whereas braking falls into the void. The new bounded coast proof
follows one registered, active pad catch and its repeated velocity pushes all
the way to an affordable supported landing with stopping room. Initial contact,
steered catches, teleporters, unknown destinations and a second different pad
remain unproven. Runtime qualification of this additional correction is pending.
The later hit in that same pad flight also has surviving coast controls; the
earlier pose isolates where the controller first spoils the reachable catch.
`bot_debug 4` now records a refused ground-slide proof's exact position,
velocity and collision normal to distinguish its ground and air phases.

## Complete catalog and renewed long matches, 4 November 2026

The `ee6207834` build completes all 33 declared map variants against the installed
`pak019.pk4` catalog (`26d64afeaa9df4716310a36c50ffe47e09fcf3a024bf2be6184e1e410b511d41`).
Each run has six skill-4 bots, a 60-second script and at least 52.960 observed
simulation seconds. Together they record 447 combat kills, 728 traversals,
156 stuck events, four goal give-ups, three no-route events and no traversal
timeouts. Runtime hashes stay unchanged. DM5's single world death passes the
strict terminal-state enemy-impulse proof; every post-hit input dies and all
three no-hit controls survive. Thus all 33 pass the derived death/duration gate,
while the original DM5 report remains a failure. The raw reports, catalog/hash
manifest and derived assessment are `.tmp/mp-roadmap/catalog-current-staged/`;
the causal proof is `catalog-dm5-classification/classification.json`.

This is a functional sweep on a shared host, not controlled timing or a blanket
bot-quality pass. CTF6 contributes 80 of the stalls and no combat kills. An
isolated walking probe and a six-bot repeat on the subsequent `054fbeb7b` build
confirm a stalled approach near `(-735, 2576, 0)`: the route uses a rim node
whose broad landing support fails the edge guard. The earlier brake oscillated
there; proportional braking settles without finding the supported parallel
line. Both runtime probes and their surface verdicts are retained in
`ctf6-probe/` and `ctf6-probe-baseline/`. The baseline lacks terminal-physics
reporting, so its movement observation is not a survival or death-exclusion
qualification.

The targeted `80ff1d253` correction lets a slow, grounded bot creep onto a
supported parallel line instead of braking indefinitely. Four sideways headings
are tested against continuous landing support, collision/footing clearance and
unplanned transport volumes; active knockback and faster drift retain braking.
The original CTF6 probe now reduces its distance to the walking corner from
27.998 to 13.236 units and finishes alive and grounded, with unchanged runtime
hashes (`ctf6-probe-fixed/movement-assessment.json`). The production decision
fixture passes on Windows and under Linux ASan/UBSan and rejects four additional
reversions. Together the decision, geometry and hazard fixtures reject 28
reversions. This isolated movement proof does not replace the broader death
or timing gates. A 120-second six-bot CTF6 repeat records 110.768 observed
seconds, 11 combat kills, 52 traversals and no world deaths, no-route events,
goal give-ups or traversal timeouts; 26 stuck events remain. All ten DM8/CTF5
positive recovery outcomes also pass their unchanged criteria. Reports are
`ctf6-rim-fixed/report.json` and `air-rim-fixed/report.json`, with client
`6c22b97c3151f7b3c6b65f8ab8e68060cdff70187f457bfbcee395d90c5e9b28`
and MP module `6733e377f6509c1edcc8dd77ed896687bd077c050f29b9cf41e866459edf9d92`.
The complete catalog and long-match evidence below predates this final targeted
correction; it is not silently relabelled as a sweep of the newer build.

The same `ee6207834` binaries also complete all four five-minute audits:

| Map | Observed simulation | Combat kills | Traversals | Stuck | World deaths | Excluded impulses | Unexcluded deaths |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| q4dm8 | 284.288 s | 102 | 158 | 1 | 3 | 2 | 1 |
| q4ctf3 | 285.200 s | 43 | 119 | 0 | 0 | 0 | 0 |
| q4ctf5 | 284.304 s | 75 | 207 | 7 | 5 | 4 | 1 |
| q4dz3 | 285.120 s | 57 | 90 | 2 | 0 | 0 | 0 |

There are no warmup world deaths, traversal timeouts, goal give-ups or no-route
events. The complete causal assessment is `current-classification/classification.json`;
`long-match-current-assessment.json` binds it to the unchanged raw reports.
DM8 Strauss's low-health machinegun pose has surviving fixed inputs; its
controller later has 47 health but is airborne at the endpoint. CTF5 Strauss's
rail-hit replay has both a surviving fixed input and a surviving controller.
Neither isolated replay reproduces the full match's later death or supplies
an exclusion. These two unexcluded deaths keep the long-match gate open.
Positive recovery rules have not been substituted for the stricter causal
policy. No prediction/rewind default changes accompany this work.

The main checkout's `054fbeb7b` build passes three pure IPv4 joins/reconnects
under seeded delay, jitter, loss and duplication, receiving 244 fragments with
clean client/server exits (`.tmp/mp-roadmap/lan-landed/report.json`). Its stage
and hashes are recorded separately from the catalog's pinned runtime.

## Single-host timing reference, 4 November 2026

The final main-checkout `3550f0dfb` stage runs a five-minute CTF3 audit alone
on this Windows x64 host (Core i7-13700H, 14 cores/20 logical processors,
`debugoptimized`, OpenGL). Its selected reference limits are 4 ms weighted
mean bot think per game frame, 8 ms worst-window p95 and 1,000 ms navigation
generation. All three pass: **0.986 ms**, **5.000 ms** and **784 ms**, respectively.
The workload monitor records no competing game or compiler process. The
29 profile windows retain a 38.579 ms single-frame maximum; the p95 budget is
not a guarantee that every frame stays below 8 ms. These measurements qualify
this map, bot count and host, not every map or target platform.

Six skill-4 bots play 284.128 observed seconds, with 40 combat kills, 99
traversals and a 1-2 capture score. There are no stuck, no-route, goal give-up
or traversal-timeout events. One world death follows an enemy nail splash,
so the original report fails the independent safety gate despite passing all
timing limits. The report is `.tmp/mp-roadmap/timing-ctf3-landed/report.json`,
with host/limit metadata in `timing-host.json`. The game exits cleanly and
runtime hashes remain unchanged: client
`b79fdc80e522765d7c8efddbc248b6cfdd264354bc496ac1afce99fbd56dd52a`,
MP module `0ef21de90880f7fd492a03c32aca070be6bf8a55bcebb3e1a894fec2927db22a`.

The completed `timing-ctf3-classification/classification.json` retains that
death as navigation/control: three fixed inputs land with 108 health and the
controller lands with 113. All three no-hit controls also survive. Thus the
enemy impulse is not a valid exclusion, and the isolated controller does not
reproduce the match's later departure. The derived `assessment.json` beside
the timing report passes only the timing limits and fails the selected overall
gate. Investigating that full departure remains part of MP-02.

## Moving crusher and self-blast safety, 4 November 2026

The renewed four-map audit on the earlier recovery build (`5efd1987e`) completes
with unchanged runtime hashes. All runs use six skill-4 bots and a 300-second
script, including warmup in the strict zero-world-death gate:

| Map | Observed simulation | Combat kills | Traversals | Stuck | World deaths |
| --- | ---: | ---: | ---: | ---: | ---: |
| q4dm8 | 284.528 s | 106 | 177 | 1 | 6 |
| q4ctf3 | 284.512 s | 35 | 122 | 6 | 0 |
| q4ctf5 | 284.480 s | 79 | 195 | 3 | 4 |
| q4dz3 | 284.432 s | 66 | 104 | 4 | 0 |

There are no traversal timeouts or goal give-ups. DM8 also has one no-route
event. CTF3/DZ3 pass the selected death/duration gates; their stalls remain
visible. DM8/CTF5 fail until a complete causal proof supports an exclusion.
Reports are `.tmp/mp-roadmap/bots-*-recovery/report.json`. These shared-host
runs do not qualify timing. Causal replays use independent, hash-verified copies
of these exact binaries in `.tmp/runtime/candidate/`, with a recorded freshness
override solely for the earlier match. New staged builds cannot replace that
evidence.

The complete assessment in `recovery-classification/classification.json`
retains two navigation/control deaths and four excluded impulses on DM8, and
two navigation/control deaths and two excluded impulses on CTF5. Besides the
self-blast and crusher cases below, DM8 Voss has a recoverable earlier state
followed by several later hits; its isolated controller remains alive but
airborne at the endpoint. CTF5 Voss fails the no-hit counterfactual proof.
Neither is a proven exclusion, and neither isolated result establishes the
full match's later cause. The original reports remain failures.

One CTF5 death occurs at `(834, 321, 604)` under `func_door_1`'s crusher.
The stock map binds a 1000-damage trigger to this translating door, initially
disabled and enabled when it moves. The graph priced only its initial bounds,
so the future crush floor remained usable. A deadly trigger on an unbound
binary mover now reserves its entire translation envelope, even if initially
off. Endpoint bounds use the current world-space offset, so gathering during
motion or after endpoint reversal produces the same result. Arbitrary moving
parents, parent-bound movers and disabled static volumes keep their existing
unmodelled/priced policy; this does not introduce general moving-platform AI.

The production mover/gathering/floor fixture passes on Windows and under Clang
ASan/UBSan on Linux x64, including four rejected reversions. In the actual CTF5
map, both hull and support probes at `(834, 321, 544.25)` change from accepted
to refused. The safe walkway at `(518, 670, -183.75)` remains accepted, and the
fixed recovery control lands alive. Before/after reports are
`.tmp/mp-roadmap/crusher-baseline/` and `crusher-fixed/`; the latter uses
`1c07ac4a9`, client `9c40f14e95bc7ed123a2cf040c0b16621204a3f16bce58f82719ed60dd350750`
and MP module `74ffb9af96da668ab21cec99ebe60b886ea2dec4ae5e19838b9ddd219f6ef73e`.

DM8 Sledge's retained failure includes its own rocket splash after a survivable
enemy machinegun hit. Such a death cannot receive an enemy-impulse exclusion.
The firing guard checked the health trade but did not check the shooter's own
post-blast trajectory. It now predicts self knockback, including multiplayer
mode, falloff, damage-power/self-damage scaling, integer truncation, self push
scale and the friction-free timer, then requires a supported slide/flight.
Cover, an out-of-range blast and a proven landing still permit the shot.
Even a zero push retains the ground-friction interval started by player physics.
The decision fixture rejects removal of either self scale or the timer.
This fixes a demonstrated policy gap; the recorded firing frame was not
captured, so it does not prove that this guard alone would prevent that exact
match's later self-blast. It also does not model future changes in player input
or claim that every affordable landing has affordable fall damage.

The integrated `ee6207834` build passes all ten recovery outcomes in
`.tmp/mp-roadmap/air-current/report.json`, preserving the explicit settled-input
and resumed-navigation criteria above. Both map processes exit cleanly and all
runtime hashes remain unchanged: client
`225b9fa9b01e21503e8982e05d20a5f90027fb2a3daa9719f3d6a1031847f3cf`,
MP module `2ce84809568486fdddf93b21dd1a61bdd92c98a7652b0c7f8d1338b984a9be55`.
The self-knockback decision controls pass on Windows and Linux with ASan/UBSan;
the current decision, geometry and hazard suites reject 23 total reversions.
The prior integrated crusher build also passes three pure IPv6 reconnects under
seeded delay, jitter, loss and duplication (`lan-crusher-integrated/report.json`).
This covers the merge with the latest save/restore changes, without qualifying
remote weapon presentation or promoting prediction/rewind defaults.

A subsequent zero-push regression retains the knockback friction interval even
when the authored push scale or blast direction is zero. It adds one reversed
control to the decision fixture; those exceptional branches need a trajectory
proof too, because `idPlayer::Damage` still starts the timer.

## Terminal-state long-match audit, 4 November 2026

Fresh five-minute, six-bot, skill-4 matches on `6357fd37e` retain strict
world-death failures. DM8 records 284.160 simulated seconds, 107 combat kills,
162 completed traversals, two stuck events and four world deaths. CTF5 records
284.144 seconds, 80 kills, 219 traversals, six stuck events and four world deaths.
Neither records a goal give-up or traversal timeout. Runtime hashes remain
unchanged; reports are in `.tmp/mp-roadmap/bots-dm8-final/` and
`bots-ctf5-final/`. These shared-host runs do not qualify controlled timing.

The completed terminal-state causal assessment is recorded separately in
`.tmp/mp-roadmap/reconciled-classification.json`: DM8 has three excluded enemy
impulses and one navigation/control death (Voss); CTF5 has two excluded enemy
impulses and two navigation/control deaths (Sledge). Every exclusion retains
its complete post-hit envelope and surviving no-hit counterfactual. The raw
match failures and original interrupted causal reports remain unchanged.

The checker now treats a contiguous shotgun burst as one impulse only when
victim, time, attacker, origin, damage definition, knockback and every health/
velocity transition agree. The counterfactual removes the entire burst and
retains its component pellets. After a complete latest-hit replay proves that
both the hit and every no-hit control were already lost, the bounded earlier-hit
search tries stronger kicks first. Later machinegun taps can otherwise exhaust
the search before its rocket cause. Missing trials or a surviving post-hit
input still prohibit fallback; the exclusion proof is unchanged. The grouped
Cortez and prioritized Sledge reruns retain exact-command/runtime checks for
reused completed batches and fresh logs for changed trials. Their provenance
is in `grouped-shot-classification/` and `prioritized-impulse-classification/`.

Voss's DM8 hit has a surviving northeast input onto a raised lip. Sledge's
first CTF5 rail has surviving fixed inputs onto the lower walkway, while the
controller leaves a marginal touchdown. Sledge's second CTF5 death remains
unisolated: its earlier rocket controller lands, then begins a later pad flight,
so that isolated state does not reproduce the match's subsequent departure.
Neither a live bot at an arbitrary airborne endpoint nor a brief touchdown
closes the gate. Replays also count drowning, slime and lava obituaries, so a
respawn cannot disguise a liquid death as survival.

## Earlier knockback control and marginal landings, 4 November 2026

The recorded Voss and Sledge poses expose two additional recovery gaps.
Grounded knockback can need steering before the airborne flag changes. The
bounded flat-floor proof now tries the held input, including coasting, then
sixteen headings through the remaining knockback and ordinary ground control.
It requires a safe stopping reserve or a proven flight, and refuses slopes,
steps, walls, slick floors, liquids and custom friction. A new sliding state
keeps that input until takeoff or safe braking. Recovery commands also respect
the base speed used by prediction when Haste or Scout boosts movement.

The DM8 ledge has an affordable physical hull touchdown even though it fails
the broad support required for ordinary routes. When every supported landing
fails, involuntary recovery now keeps such a touchdown as its last resort,
then brakes on contact as the fixed-input replay does. This is a recovery
heuristic, not a new safe-route admission or an impulse-death exclusion.
Lethal, unaffordable and transport landings cannot supply that fallback.
The held input survives replanning when it remains the best physical contact.
The air predictor also models the player's descending step over a nearby lip;
a continuously supported inward rim landing retains its proved movement.

Several earlier hypotheses were insufficient in the real maps despite passing
native controls. The original six cases, two new controller poses and two fixed
input controls are retained in `mp_bot_air_recovery_replay.py`. Failed
`air-integrated-final`, `early-slide-candidate` and `air-recovery-qualified`
reports remain in `.tmp/mp-roadmap/`, alongside the focused diagnostic runs.
The speculative wall-stop and ground-turn simulators were removed.

The CTF5 controller can recover, stop, then select a new pad route before the
six-second trial ends. Its isolated positive regression therefore reports its
state at three seconds before releasing bot commands, then lets player/world
physics run for three more seconds. It passes only if already alive, grounded
and effectively motionless at release, with no death and a grounded live final
state. Missing or duplicate release markers, speed, airborne state and deaths
fail closed. Causal exclusion envelopes continue their original complete trials
and do not use this release option.

The final ten-case replay retains its original strict report in
`.tmp/mp-roadmap/air-recovery-final/report.json`. Eight positive cases finish
alive and grounded, and the deliberately lost pose still dies. The remaining
DM8 controller lands alive, completes a drop and a teleport, then starts a new
jump; it is alive but airborne at the six-second endpoint. The separate
`recovery-assessment.json` records that as positive recovery followed by
navigation, without changing the original failure or granting a causal
exclusion. This opt-in assessment requires two completed traversals after
touchdown, the bot's own markers, a live final state and no world death.
Missing markers, a single completion, another bot's actions and death all fail.
All ten expected recovery outcomes pass this assessment, with clean exits and
unchanged hashes: client
`2154c46339a1bc10cb92a3caf770544a4c25bb79a5b22bfb1a25ceeb1409f536`,
MP module `049b97009335419d47b56bf8d82b56ca5e28af039e66669a91755aa0a58be57d`.
These isolated probes do not qualify continued long-match navigation.

The final native decision and geometry suites pass on Windows and Linux x64,
with Linux Clang ASan/UBSan and sixteen failing reversal controls. Controls
include early steering, slide friction/acceleration, affordable marginal contact,
actual-origin prediction, descending steps, input retention and rim braking.
Ordinary route support, hazards, transport refusal and boosted command speed
remain checked. Logs are `.tmp/mp-roadmap/bot-*-touch-brake*.log`.

## Long-match recovery audit, 3 November 2026

The renewed five-minute, six-bot, skill-4 DM8 audit records 284.128 simulated
seconds, 106 combat kills and 152 completed traversals, with no stuck events,
goal give-ups or traversal timeouts. Six world deaths remain subject to the
strict causal policy. The match's runtime hashes stayed unchanged (client
`241211745bb14c70`, MP module `f35a22ba1c45d83f`); its report and log are in
`.tmp/mp-roadmap/bots-dm8/`.

The first causality run was interrupted by the freshness check after a newer
build was compiled. That report remains a failure, with no exclusions. The
complete rerun uses independent copies of the original, hash-verified match
binaries in `.tmp/runtime/baseline/`. Its deliberate freshness override applies
only to causality for that recorded match. Each victim group has its own save
directory and port; all batches retain hashes and clean-exit checks. The
combined report in `.tmp/mp-roadmap/impulse-rerun/classification.json` historically
classified four deaths as enemy impulses and two as navigation/control failures.
Those exclusions are **not current qualification**: the follow-up below found
that the replay checker accepted an earlier touchdown even if the bot was
falling again at the end. All six remain counted until the stronger check is
rerun against the recorded runtime with terminal-state instrumentation.

Rhodes's rocket hit leaves five health. The hit-position controller survives,
but the later recorded recovery pose near `(528.1, 686, 328.2)` reproduces a
fatal five-point landing with both the later route target and a return target.
Those poses come from rounded diagnostics, not a full match snapshot. Voss's
later machinegun hit occurs after the fall was already lost; tracing back to
the preceding rocket finds two surviving fixed inputs while the controller
falls. Original controls and the route probe are retained alongside the audit.

CTF3 and DZ3 also pass five-minute audits on the integrated `62ee95276` build:
CTF3 records 284.192 simulated seconds, 40 kills and 116 traversals; DZ3 records
284.320 seconds, 59 kills and 91 traversals. Neither has a world death, stuck
event, give-up or traversal timeout. DZ3 records one refused pickup. These are
in `.tmp/mp-roadmap/bots-ctf3/` and `bots-dz3/`.

CTF5 records 284.256 simulated seconds, 79 kills, 211 traversals and two stuck
events, with no give-ups or traversal timeouts. The older checker classified
one death as an enemy impulse and two as recoverable failures (Kane and Sledge).
That exclusion also needs the terminal-state requalification described above.
The isolated controller survives both latter hit states; their later match
departures still need isolation. The strict audit fails, as recorded in
`.tmp/mp-roadmap/bots-ctf5/report.json`.

All runs share the host with other work. DM8's observed 0.714 ms weighted mean
and 3.25 ms worst-window p95 ceiling meet the selected reference budgets, but
the competing-runtime/compiler check rejects controlled timing qualification.
The strict broader safety and timing gates remain open.

## Knockback slide and replay corrections, 3 November 2026

The follow-up isolates two failures in real DM8 physics. A five-health bot
survives a rocket if it coasts, but steering toward its southwest walking
goal changes the slide before airborne recovery begins. A second rocket
lands on a rim; full reverse commands at low speed then alternate direction
until the bot walks off again.

Recovery now preserves a proven coast through a grounded knockback interval,
using the physics object's remaining knockback time. The new bounded proof
follows flat-floor movement without friction, then requires a survivable fall
or stopping reserve. Slopes, walls, steps and transport intersections remain
unproven by this narrow check. Unplanned knocked flights take control early,
zero input remains a valid held plan, and edge braking fades as speed falls.
Air prediction also starts at the actual origin rather than one unit above it.

`mp_bot_air_recovery_replay.py` passes all six expected outcomes in
`.tmp/mp-roadmap/air-slide-proof/report.json`: southwest, northwest and south
route targets finish grounded at five health; the second rocket's controller
and fixed north input survive; the already-lost later pose still dies. The
client hash is `47322dfc491a57a38d10240df99aa5b475f15b95370c9daf0c36cea39272e990`
and the MP module is
`70830d96d87b540324a0bcaa157d55b7550a3aa66ea44980d4e2c2c61175f1c3`;
all runtime hashes stay unchanged through the replay. Failed intermediate
replays remain beside it, including the air-only and first-contact-only fixes.

The native decision and geometry fixtures pass on Windows and Linux x64
(Clang ASan/UBSan on Linux), including eleven deliberate reversions across
the two suites. The geometry controls cover a ground slide that crosses a
pit after its ordinary stopping reserve, affordable landing versus void,
false clearance from the lifted origin, and preservation of zero input.

The audit itself now reports `MP_PHYSICS` immediately before each trial ends
and requires a positive-health, grounded final state as well as a touchdown
and no world death. Missing terminal state fails closed. Trial markers must
match the entire name exactly once: `south` must not read `southwest`.
Negative controls cover airborne/dead/missing final state and duplicate or
prefix markers. Preserved old reports are not rewritten; separate
`terminal-exact-marker-reassessment.json` files correct the intermediate
interpretations. In particular, the original south route survived too; the
southwest route was the reproduced failure.

## Why not AAS

Quake 4's SDK routes AI over AAS, an offline navigation compile stored as a
`.aas48` / `.aas96` file next to the `.proc`. id shipped those files for the
single-player campaign only: **not one of the 49 stock multiplayer maps has an
AAS file**, and neither do community maps built with the retail tools, because
the multiplayer game never had anything to navigate. AAS-based bots therefore
have nothing to route over in multiplayer, which is exactly what the SDK's own
`idGameLocal::AddBot` reports when it refuses to add a bot.

So navigation is generated at runtime instead, from the collision world the
players themselves move through.

## The navmesh

`src/mpgame/bots/NavMesh.{h,cpp}` builds a multi-level grid navmesh at map
load.

**Generation** follows the same shape as a modern voxel navmesh generator -
rasterise the walkable surface, connect what a walking agent can traverse - but
the "rasteriser" is the engine's own collision system, so a link exists only if
the configured player hull clears the sampled move:

1. **Seeds.** Spawn points, items, jump pads and their landing targets, and any
   player already in the map. These are places the map guarantees are standable,
   and flooding out from them avoids probing the solid majority of the world
   bounds.
2. **Hazards.** `trigger_hurt` volumes are read before the flood. One that gibs,
   deals 100 or more per touch or 30 or more per second refuses the ground it
   covers outright, as lava and slime do; a survivable one, one that starts
   switched off or one riding a mover makes its ground cost 384 more to cross.
   A mover sitting just above a lethal volume (a trapdoor) or one that crushes
   is priced the same way.
3. **Flood fill.** From each node, the eight neighbouring grid cells are probed
   with `TryStep`, which sweeps the player's hull - `pm_bboxwidth` by
   `pm_normalheight`, with `pm_usecylinder` selecting the same side count and
   alignment as player physics - flat,
   then lifted by `pm_stepsize`, then lifted by `pm_jumpheight`, and drops
   whatever it reaches onto the floor. The cheapest lift that works decides the
   link type. A smaller central support probe keeps ordinary nodes away from
   floors touched only by the outside edge of the hull. Jump and drop landings
   also need support around that centre, so a decorative rim beside a pit is
   not treated as stable ground. A node whose hull still hangs more than eight
   units past an edge - the last few units of q4ctf1's 64-unit platforms and
   the rim of q4dz1's catwalk, where a column of cell centres falls four units
   past the edge with twelve of the hull's thirty-two units on the floor - is
   kept, since a player can stand there and a narrow catwalk may have nothing
   else, but is a *rim node*: every link onto it costs a cell more, so a route
   runs along an edge only where the edge is all there is. A wall or a stair's
   next tread beside a node is not an edge. The probes lift for legitimate slopes and
   require the returned floor to remain within a step of the expected height.
   A node is keyed by grid cell *and* floor height, so a catwalk over
   a corridor is two nodes in the same column. Drops reach 512 units and pay
   for the fall damage they would cost. A proven level walk links back at once,
   which halves the build's collision sweeps. Every jump-height lift also needs
   the full jump trajectory proof: lifting a box over a railing and dropping
   it vertically does not model momentum over a narrow lower ledge or raised
   decorative lip. A lift the ceiling cuts short is still a lift, as the
   player's own step-up is, so a threshold under a low lintel is a step and
   not a wall. Near-ground air steps preserve momentum as multiplayer
   physics does; a blocked ascent can slide up a riser, while partially
   completed air steps fail closed. Drops also simulate slow and full-speed
   walk-offs, including the permitted sideways approach velocity, through
   touchdown and supported floor along the braking distance. Grounded steps
   over low lips remain usable under ceilings too low for a jump. A vertical
   projection onto a small island is insufficient.
4. **Gap jumps.** From a ledge with a straight run-up behind it, each direction
   is searched for the first floor beyond a gap within a running jump's reach,
   and the jump is linked only when full hull sweeps through touchdown at both
   85% and full approach speed prove it clears and lands. The simulation uses
   the bot's direction-to-corner air input and multiplayer air acceleration;
   landing requires supported, nonlethal ground with a walkable connection to
   the destination. Passing above an island while still rising is insufficient.
5. **Off-mesh links.** `rvJumpPad` and teleporter `idTrigger_Multi` volumes get
   a one-way link from the volume to where they put the player. A pad's target
   is the apex of its arc, not its landing, so the flight is integrated from the
   pad's own launch velocity at the multiplayer frame rate and swept with the
   player's box until it comes down; q4dm1's pad 24 aims over a ledge and lands
   players on the floor below. A pad can put a player down in more than one
   place, as Quake III's AAS knows: besides its natural landing, the supported
   node nearest the apex on each floor level within 256 units is flown again
   with air control steering for it, and linked when the flight really comes
   down within 32 units of it from the pad's centre and from the middle of
   every side a standing box can touch the volume from - a bot is thrown where
   it first touches the pad, not from its centre - up to four landings a pad. That is the only way
   onto the ledge beside q4dm1's vertical pads, whose arc falls straight back
   down their shaft. The natural landing's node is the nearest one to the
   touchdown with floor under the middle of the hull, and the flight from the
   centre has to come down on it flown as the bot flies it - ridden where the
   pad's own arc comes down on the node, steered for it otherwise. (q4ctf1's
   pad 17 slides its arc along a wall and comes down ten units past its node;
   steering for that node takes the flight off a ledge 192 units lower, and a
   bot rides it instead. A directional pad entered from its side throws a bot
   down beside the ledge, as q4dm5's forty pads do, and the bot recovers from
   there: the natural landing is not held to every side.)
   q4dz1's vertical pads sit in a shaft whose ledge the arc from the centre
   just catches; the nearest node to that touchdown hung half a hull over the
   shaft, and from the front of the pad, where every bot enters, a flight
   steered for it came down short, into the shaft, and bounced until the
   traversal timed out. A bot rides a natural landing without steering, as long as
   the ridden arc lands on or past the node and within a box of its line -
   braking onto the node slowed flyers under the bots behind them, and over
   q4ctf5's gap they met in the air. Steering is `NavAirSteer`, shared with the
   bot in flight:
   it aims for the horizontal velocity that arrives over the landing as the arc
   comes down to its height, rather than at the landing itself, which overshot
   and swung back. Nothing that comes down on the pad itself is a landing, and
   a pad with no other is a trampoline that gets no link. Each landing costs
   the ground a bot would have run while in the air, never less than 128, so a
   second and a half aloft beside a vertical pad is not a cheap way past it. On
   a stock Quake 4 multiplayer map these links are what keep the graph a single
   component.
6. **Transport sealing.** A pad or teleporter takes whoever touches it, so every
   node whose standing box touches a live volume - tested against the brush
   itself, not its bounding box - keeps the transport, to each of its landings,
   as its only exits. A route either takes the pad or goes round it; it can never walk across
   one by accident. Links into the volume stay - entering it is how a route
   takes the transport - and a route never starts or ends on one. Shortcuts are
   held to the same brush: a line is tested by sweeping the standing box along
   it, and 48 units under it, against the volume's own clip model, and only a
   line whose box starts touching the brush counts as leaving it. A diagonal
   pad's bounding box covers floor beside the pad that sealing leaves walkable,
   and a line from that floor used to skip the pad altogether.
7. **Compaction.** Each node's links are rewritten into one contiguous run, so
   every search walks memory in order instead of chasing a list through a
   quarter of a million entries.
8. **Reachability.** Strongly connected components and the transitive closure
   of their condensation answer "can A reach B" exactly in constant time, so a
   goal nobody can reach - an enemy standing in a one-way pit - is refused
   without a search.
   The build line reports the share of nodes in the largest component and the
   dead ends - nodes with no way off at all - that a map sweep reads for
   places a bot can be trapped.
9. **Landmarks.** Eight far-apart nodes of the largest component carry exact
   travel costs to and from every node (ALT). The triangle inequality turns
   them into a lower bound on any route, which guides A* around walls and stays
   tight across the cheap long-range transports that collapse a straight-line
   bound. Each node's eight pairs sit in one 64-byte row, so a bound costs one
   cache line.

Traversal types are `WALK`, `DROP`, `JUMP`, `JUMPPAD` and `TELEPORT`. Every
navmesh sweep tests the world and solid entities but never a body: multiplayer
players carry `CONTENTS_SOLID` for their combat box, and a sweep that collided
with them refused route snaps on the very floor a bot stood on.

**Routing** is A* over a binary heap with weighted travel costs, guided by the
larger of the landmark bound and the straight-line distance scaled by the
graph's cheapest cost-to-distance ratio; both are consistent, and a node the
landmarks prove cannot lead to the goal is never opened. Ordinary walking pays
distance, jumps and drops add a safety/time penalty, jump pads pay for flight
time as running distance, and teleporters retain their short transport cost. Search scratch is tagged by a
serial number rather than cleared, so a query costs only the nodes it opens. A
selected route is then swept against the solids that can move after the build -
movers, doors, plats and loose physics objects - and only those; a stale edge is
excluded and A* retries. Ordinary unlocked touch/proximity doors count as open,
because approaching them opens them; locked, no-touch, keyed or scripted doors
stay barriers. The sweep asks only the recorded dynamic solids whose bounds the
link crosses, never players or static props: the earlier form swept every solid
entity and passed the static ones it met, two at most, so the bot's own body
spent the first pass on the first link and a third static blocker ended the
proof as "clear" with the mover beyond it untested. An excluded edge is a stamp
in a per-link table rather than an entry in a list that every relaxed edge of
every repair pass scanned. The cell hash mixes both grid coordinates; the plain
sum put every anti-diagonal of the grid into one chain, about eighty nodes deep
on q4dm5, walked for every cell a snap or a search looked at.

An entity's node is cached per spawn and position, but a failed snap only for
five seconds: the snap sweeps movers where they stand, and an item whose alcove
a plat was parked across on the first decision tick used to stay invisible to
every bot for the whole map.

An injured bot also qualifies selected jump, drop and pad landings against its
current health and armour. The limit comes from the player's fall thresholds
and resolved multiplayer damage declarations. Predicted impacts use the same
water reductions and no-damage surface flag as `CrashLand`. Unsafe edges are
excluded only from that search, so healthy bots and safe alternate routes keep
using the shared graph. A grounded pre-entry check catches damage after route
planning, brakes and abandons the old goal; its cached proof is reused while
the route and survivable tier stay unchanged. An already launched flight keeps
its landing controller.

Route endpoints in the air - a jumping bot, an enemy mid-jump - are projected
onto whatever surface is under them before they snap to a node. A snap or a
string-pulled shortcut must pass `WalkableLine`: a box sweep, floor samples
along the line, no lethal ground, no step onto a transport, and no rise or fall
steeper than a walk, so a bot under a balcony never routes from the balcony. A
sweep stopped within its last two units has arrived: a player flush against a
wall is a legal endpoint the clip epsilon stops a box just short of. The line
also refuses ground more than a step below both of its ends - a pit between
them that the graph classed as a drop in and a jump out, each with its own
proof and penalty, which a chord across a sunken panel or a drain used to walk
the bot into - and survivable pain floor the graph priced to avoid, unless the
walk starts and ends in it. The sweep is a straight line between two lifted
endpoints, so on a line that climbs it rises with the line and clears the top
of a riser the player on the floor walks into: q4ctf1's string-pulled walks
from the low floor onto the lip of its 64-unit platform, and onto the side of
its ramp, passed both sweeps and every drop-tolerance sample, and the bot
pressed against the wall under its corner until the stuck detector threw the
route away. The floor samples say where the ground really rises, so
consecutive samples, the endpoints included, climb more than a step only
where the ground sampled between them climbs at most a step on either side,
which a 45-degree slope or a stair of player-height steps does and a riser
does not, however the samples fall on it. A bot also counts a corner as
reached up to 28 units short of it or beside it, so a string-pulled leg that
climbs or descends more than a step is proven from either side of its corner
as well: q4ctf1's lip nodes along the north edge of its ramp stand on a
sliver of the ramp a step above the floor beside them, and the line from one
ran diagonally up the lip while the bot, a hull's width north of it on the
floor, walked into the ramp's side. A hull half over an edge still finds the
floor under its other half, so a line along a rim - q4ctf1's platform edge
nodes, q4dz1's catwalk - walked bots the whole way with their feet hanging
over the drop until a strafe, a shove or an unstick push took them off it;
the ground a support box's width to either side of every interior sample has
to be there as well, within a step, which a stair's next tread or a wall
beside the line still is.

The node chain is then string-pulled: a corner is dropped when the shortcut is
proven, and smoothing never skips a link that has to be entered deliberately.
Every jump, drop, jump pad and teleporter retains both its entry and exit. A jump - up a
step too tall to walk or across a gap - lands only where floor reaches within
8 units of the box's middle: a standing box rests on any overlap, so flood fill
lays nodes that hang off a platform by all but a sliver (the corner of q4dm1's
turned crate held one by six tenths of a unit), and a jump aimed at one comes
down beside the platform every time. A gap jump whose first floor is such an
edge lands one cell further in. A
gap jump - or a jump up more than two steps, such as q4dm1's crates - gets a
straight run-up corner in front of its takeoff. A jump that comes back down on
its takeoff floor short of the middle of its link fell short; the bot goes back
to the run-up and takes it again at speed, twice, before the stuck detector
decides. Height alone cannot tell, since a level jump lands at its takeoff's
height whether it made it or not. A goal that
stands in a pad's or teleporter's reach - an item beside one, an enemy at its
edge - ends its route at the nearest node, because nobody can stand exactly
there without being carried off. Whether it does is judged against the
volume's own brush, as sealing judges nodes: a diagonal pad's bounding box
covers floor beside it that the pad never touches, and an item standing there
read as "in the pad", so its route was cut a cell short and arriving picked up
nothing. Two other final-leg refusals are not real: the
goal's own snap already proved the walk the other way, which stands; and a goal
set against a wall, such as an item in an alcove, can sit where a player's box
does not fit, so the route ends where the box stops within touching distance.
A chase may also settle for the nearest node on the target's floor when the
target stands where no node is proven walkable, such as a tight diagonal
corridor; item routes stay exact.

**Goal pricing** answers "which of these is worth going for" without a field
per decision. Every candidate gets a landmark lower bound on its route cost,
so its weight over that bound is the most it can score; candidates are priced
exactly, best bound first, by a corner-free A* (`rvNavMesh::PathCost`), until
the best few exact scores beat everything still unpriced. The top few are then
exact and FindPath proves only the winner. On `q4dm5` this expands about 6% of
what a whole-map Dijkstra field per decision settled. Where the bounds are weak
- long detours around pads and drops - each exact search would re-expand the
same ground, so past a work budget the candidates still unpriced are priced
from one bounded field and a decision never costs much more than a field.

Typical output for `q4dm5` at the default 24-unit cell is about **27,800 nodes,
204,000 links, 316 gap jumps and 44 sealed transports**, built in under two
seconds in a debug build. Generation is lazy - it happens the first time a bot
is added, so a server that never uses bots never pays for it.

## The bot

`src/mpgame/bots/Bot.{h,cpp}`. A bot is not an entity. It takes a real client
slot through `idNetworkSystem::AllocateClientSlotForBot`, spawns a real
`idPlayer`, and each server frame writes the user command that a remote player's
packet would have delivered. Everything downstream - movement physics, weapons,
damage, scoring, the scoreboard, game type rules, team logic - therefore treats
it as an ordinary player with no special cases.

Bot slots are excluded from outgoing unreliable-message queues and recipient
PVS checks because they have no network connection or snapshots to drain those
queues. The message's subject may still be a bot: human spectators following
it receive their feedback, and demo/repeater recording remains intact. The
documented `clientNum == -1` broadcast also reaches remote human recipients.

In team games, a newly allocated bot reserves the least-populated side before
its player entity exists. Pending players who intend to join count toward the
server's ordinary auto-balance check, so several `addbot` commands issued in
one frame still alternate sides instead of all inheriting the same team.

Movement intent and combat intent are deliberately separate. On each decision
tick a bot compares the current mode objective, useful pickups, an enemy chase,
and directed roaming, commits to the strongest reachable movement goal, and
keeps aiming and firing at the best perceived opponent while it travels there.
A flag carrier therefore continues running home through a firefight, and a
wounded bot can withdraw toward health without becoming harmless. Short goal
commitments and a material switch margin prevent equivalent choices from
chattering every quarter-second; an urgent rule change, such as a dropped flag
or enemy carrier, can still replace the route immediately.

Replacement routes are built in scratch state and committed together with their
goal identity only after a successful search. An unreachable challenger leaves
the current valid commitment intact. Repairing the same goal retains its progress
history and give-up deadline; only actual progress extends the deadline. A new
roam gets its own distance baseline, and exhausting every route clears stale
movement before off-mesh recovery takes over. Pickup commitments are rechecked
against the current inventory on each decision tick. A bot collecting its own
item does not queue an item-denial reaction.

Perception skips visibility traces for opponents that cannot improve either the
nearest or best visible choice, while always checking the current target for
continuity. Item selection rejects out-of-range pickups before inventory work
and probes navigation only for candidates that can enter the ten-item shortlist.
These filters preserve target ordering, directed route proof and team claims.
Once an enemy leaves sight, chase prediction and weapon choice use the last
observed position; combat strafing no longer follows the enemy's hidden live
position. Projectile threat rejection includes the larger of direct-hit clearance
and explosion radius, so nearby resting explosives and distant splash impacts
remain eligible for the detailed trajectory check.

*How well* and *in what manner* a bot executes those decisions - how far it
notices you, how long it takes to react, how steadily it holds an aim, how close
it wants to fight and what it wants to fight with - comes from its resolved
personality, which is the next section. Dead, it holds attack, which is how
`idPlayer` takes a respawn. Spectating is deliberately not the same thing:
attack cycles the spectator's view target rather than rejoining, so a
spectating bot clears `wantSpectate` and sets `forceRespawn` instead - the same
request a human makes on leaving the spectate menu - and lets the game type
decide when it gets in.

### Mode objectives

Objective discovery runs only during a live match, rejects stale players and
entities from other multiplayer instances, and supplies rule urgency to the
ordinary path-aware goal selector. The currently supported mode families are:

- **CTF variants.** Standard CTF, One Flag, Arena CTF and Arena One Flag use a
  deterministic team role allocation informed by navigation route bounds.
  Carriers follow the
  correct base or their instance's ordered assault-point chain; the nearest
  useful teammates return a dropped flag or intercept an enemy carrier, escorts
  form around a friendly carrier (with extra help when that carrier is hurt), a
  majority attacks the active enemy or neutral flag, and deliberately unassigned
  bots defend the capture end instead of joining a team-wide dogpile.
- **Freeze Tag.** During a live round bots are uniquely matched to nearby frozen
  teammates and remain at the body long enough for the rescue rule to act.
  Rescue urgency rises as more of the team freezes, when only one rescuer is
  left, and when enemies pressure the body.
- **DeadZone.** Bots read zone ownership and deadlock state to decide how many
  teammates should intercept, escort or contest. Carriers hold a valid authored
  control zone, while available artifacts are uniquely distributed among
  collectors. Maps whose control zone explicitly does not require the artifact
  are respected, with a larger contesting group for hostile or deadlocked zones
  and a smaller anchor after control is secure. Both ownership slots reading
  TAKEN is read as a contest: the rules used to clear only the Marine slot,
  where the deadlock is recorded, when the zone changed hands, so the loser's
  slot kept saying TAKEN for the rest of the hold and each team picked a side
  at random. The game now keeps one holder at a time as well, which the HUD's
  zone owner reads from the same slots.

These are movement objectives, not blinders. Enemy perception, aiming and safe
fire continue throughout the route. Escorts occupy stable trailing formation
lanes projected onto navigation rather than standing on their carrier, while
base defenders hold a useful perimeter instead of pinning themselves to the
flag pedestal.

### Pickups and route choice

Item choice reads the bot's live inventory. Routine health is ignored at full
health, major health remains useful for an overstack, armour scales with the
current deficit, missing weapons outrank duplicates, ammo matters only below
capacity, and major powerups remain worth a detour. Flags and the DeadZone
artifact are excluded because the mode objective layer owns them. `itemFocus`
then scales what the pickup is worth against an enemy - a roamer plays the
map, a hunter barely looks down - while the need-driven bonuses for arming up,
health at the edge of death and a pickup a few steps away stay what they are.

Every useful pickup in range is a candidate. Goal pricing (above) returns the
best few by utility over route cost, including the exact legs onto the mesh and
off it, and the bot proves a real route to the winner, falling through to the
next when live geometry blocks it. Existing claims from teammates reduce a
pickup's value rather than hard-locking it, spreading the team across resources
without preventing two bots from contesting something important. Critical
health can interrupt most tasks, while a carrier's immediate capture remains
the top movement priority except at the edge of death.

Bots time items. A bot that sees or hears a pickup - the taker, anyone within
earshot, or everyone for an announced powerup - learns when it returns, with an
error set by its `itemTiming` trait. A timed item is worth heading for when the
run arrives about as it respawns: the remaining wait discounts it, up to half,
and the bot stands on the spot for the return instead of wandering off. An
unarmed bot puts a real weapon ahead of nearly everything, a pickup a few steps
away is taken on the way even mid-chase, and a defender keeps its base stocked.
In a buying mode a bot spends its credit on the weapon or armour it lacks.

### Perception and combat

Bots honour `fl.notarget` like every other thing in the game that acquires a
target, so a developer can watch them navigate on a listen server without being
shot at.

Perception is bounded by the bot's range and field of view, then traces exposed
chest, head and pelvis points plus lateral body samples so a visible shoulder at
cover can be acquired without granting vision through the wall. Invisible
opponents stay hidden unless they recently damaged the bot or reveal themselves
by firing nearby.

Bots also hear. Each server frame records the loudest noise every player made -
weapon fire carries 1700 units, the gauntlet 700, a pickup 900 and running
footsteps 420 - scaled by the listener's `hearing` trait. A heard opponent is
located, never seen: it becomes a pursuit and patrol goal and a direction to
face, but hearing grants no shot, and a sound-only contact is worth less than a
visible one when goals are compared. Target selection weighs distance, health and armour, active
powerups, objective carriers, enemies threatening a friendly carrier, firing
opponents, the last attacker, grudges and personality, with stickiness
preventing pointless target swaps.

With nothing to shoot at, the view rests well ahead along the route - 320 units
on, at eye height over the floor there - rather than on the next corner, which
can be a step away and under the eye line; a bot that aimed at it walked every
tight route staring at the floor. A bot standing watch, a patient one on a
finished wander or one waiting for a timed item, sweeps the room 55 degrees
either side of the facing it stopped with over a seven second cycle, which is
also what lets an ambusher notice someone coming in from the flank. Movement is
unaffected by where the view rests: it is resolved in world space against
whatever the yaw is. The weapon carried between fights is the one for the fight
the personality expects, chosen for its `combatRange`: a sniper crosses the map
with the rail gun, a rusher with the shotgun and a baseline bot with the rocket
launcher or lightning gun, instead of everyone drawing the rail gun for a
thousand units of empty ground and switching when the fight starts at close
range.

Combat movement blends each character's preferred range with the weapon in
hand, so a shotgun encourages a closer engagement while a railgun preserves
space. Each fight is weighed as a trade - health and armour, the best weapon
each side holds and any active powerup - and a bot that cannot win it breaks
off toward health and armour while still shooting; how bad the trade has to be
is the character's aggression. A bot carrying quad goes hunting, and only a
well-armed, healthy bot turns toward an enemy carrying a powerup rather than
feeding it. When sight is lost, a bounded pursuit point projects the enemy's cached
position and velocity; its confidence and lead fade with memory age instead of
making the bot either stop immediately or track an opponent forever. Dodges test
the local route and floor on both sides, trying the opposite direction when the
first choice is obstructed or unsafe.

Damage is also a perception event. The damage path reports the real player or
projectile owner and attack direction, schedules one bounded dodge that
sustained fire cannot postpone forever, and promotes the attacker only if line
of sight confirms it - being hit is not permission to see through a wall. A
short-horizon scan separately advances hostile explosives through segmented,
gravity-aware hull sweeps relative to the bot's own motion. It can dodge before
a direct intercept, a predicted world impact and splash, or a slow/resting
fused explosive becomes dangerous; world cover is considered rather than
treating every nearby explosive as unobstructed. A round that keeps hurting
where it lies counts too: a napalm glob has no splash at all and burns 140
units around itself every three quarters of a second for six seconds, so its
emitted-damage radius is the hazard the scan prices, and once it has settled
beside the bot it is treated like a fused grenade. The step away from something
lying still goes straight away from it; a sidestep around the enemy could walk
over it.

Projectile aim resolves the live multiplayer projectile definition, including
launch speed, gravity and collision bounds, and iterates target motion, the
bot's own motion and drop compensation to lead rockets, nails, grenades and
napalm consistently. Instant-hit weapons are not led. Splash weapons aim at a
grounded target's feet, where a near miss still lands. Weapons the bot would be
wasting at range stay holstered past their useful reach, and the nailgun,
lightning gun and gauntlet hold the trigger instead of bursting. The weapon selector
covers every stock and expansion weapon, blends close- and long-range
suitability with character bias, penalizes splash weapons at point-blank range,
and uses score hysteresis and a minimum dwell to avoid switch loops.

Before firing, projectile weapons sweep their real hull along segmented linear
or ballistic trajectories rather than checking only a ray to the nominal aim
point. Safety projects moving teammates into those future segments, rejects
their future splash exposure (including the shooter), and accounts for world
cover around a predicted impact; safe world impacts can still support doorway
suppression. Single-shot weapons also respect readiness and cadence. Splash safety uses the
game's own damage model - linear falloff from the explosion and half damage to
the shooter - so a bot refuses a shot that would cost it more than a token
amount of health, or more than it can spare, and team damage is only
considered when `si_teamDamage` is on. The figures carry the shooter's quad or
doubler, as `rvWeapon` scales the launch power, and a direct hit is priced at
the direct damage alone, since the game hands `RadiusDamage` the struck actor
as the one to skip. With team damage off an impact within 48 units of a
team-mate is still refused, for the knockback: `RadiusDamage` shoves every hull
in the blast before it asks whether it may hurt it, and a rocket at a
team-mate's feet on a ledge is a team-mate in the void.

The cover test behind all of this passes through only an actor the shot
stopped on. The worldspawn entity must never be the pass entity, because
`idClip::Translation` skips the whole world model for it, and a wall hit used
to switch every cover test off: a bot behind a pillar read as exposed to its
own blast on the far side, a rocket into the wall was "useful" against a foe
behind cover, and a grenade landing behind a thin wall was dodged. The
speculative sweep also flies through water as the real projectile does; with
`CONTENTS_WATER` in its mask it stopped on the surface, so a foe across a pool
stood behind an impact that never happens.

That sweep runs down the axis the weapon actually fires along - the view - and
not toward the aim point. The trigger gate deliberately allows the two to differ
by up to the whole fire cone, which is 18 degrees at skill 1, so a proof run
against the aim ray would clear a trajectory the bot never takes. The
usefulness half of the same query is answered before the splash test rather than
through it: an unobstructed shot and one that stops on another hostile are
useful by construction, and only an impact on the world has to put the target
inside the resulting splash. Weapons with no splash carry a zero radius, and
routing those through a splash test would make every hitscan weapon hold fire
unless the trace happened to land on the target hull - which the deliberate aim
error is designed to prevent.

### Navigation and progress

Four things keep a bot from ever parking:

- **Off-route recovery.** A route is a statement about the floor the bot stands
  on. Once grounded, the bot compares its height with the height the current
  leg has where it stands; knocked off a ledge, carried by a pad or fallen past
  a landing, it is more than 80 units below (or, on a walk, 96 above) and the
  route is replaced at once, with the goal itself decided again. So is a bot
  more than 384 units to the side of its leg, as a teleporter or a long
  knockback leaves it, and a drop or a jump that came down more than a step
  below its takeoff without reaching its landing - momentum past the landing's
  wall, a pad beside the landing, a gap jump into its gap - since that landing
  may now be behind a wall. Nearer than those limits, a bot more than a
  corner's reach beside a walk leg, or more than a step off its height, asks
  the navmesh four times a second whether the walk from where it stands to
  its corner still holds, and re-routes when it does not: combat footwork and
  knockback left bots a hull or two off the side of q4ctf1's ramp, on the
  floor below it or the platform above, pressing at a corner a wall's height
  away - which only the stuck detector ended, a second and a half later, and
  not at all while the fight kept the detector quiet.
- **Stuck detection.** Progress means advancing a path corner or materially
  shortening the horizontal distance to the current one, not merely moving in
  any direction. Two consecutive failed samples trigger a sidestep, a jump and
  a fresh route. Vertical motion alone cannot disguise a bot hopping in place,
  while a deliberate traversal gets a bounded grace period in which to finish.
- **Goal give-up.** The deadline scales with the route's expected travel time
  and is extended only when the complete remaining path gets shorter. A stale
  goal with no recent progress is abandoned, and arriving at an item that is
  *still sitting there* abandons it immediately - that is a weapon it already
  has with full ammo or another pickup it cannot use. Abandoned goals go on a
  short per-bot blacklist. The blacklist holds several entries on purpose:
  with one slot a bot ping-pongs between two items it cannot take.
- **Off-mesh recovery.** If routing fails repeatedly the bot walks toward the
  nearest node it does know about, and wanders if there is not one. Both paths
  use the grounded momentum/edge brake; abandoning a route cannot bypass it.
  A stationary bot supported only by its outer hull may crawl toward a known
  node at no more than 32 units/s if a separate, at-most-64-unit proof finds
  nearby strong support. Every four-unit sample must retain same-height hull
  contact and body clearance. Consecutive footprints must also share a known
  floor patch, so even a narrow loss of support between samples refuses the
  escape. Gaps, walls, transports and lethal floor are rejected. Ordinary
  walking and random wandering gain no such exception.
  A recovery turn updates the command's view yaw before projecting movement,
  so physics uses the same world direction that passed the floor check.

A foe last seen inside a lethal volume - falling into q4dm5's void - is not
chased: it is dying, and no route reaches the air it falls through.

Repathing is time-throttled rather than "whenever there is no path": an enemy
standing somewhere genuinely unreachable would otherwise cost a full failed A*
search every single frame. The throttle counts *attempts*, not successes —
keying it off "am I currently chasing an enemy" leaves it dead on the failure
path, which is the only path it exists for. Goal selection, which walks every
spawned item, is throttled the same way.

Grounded movement includes a void-edge guard. A route leg is proven, but
the box drifts off it - a string-pulled corner, a dodge, a team-mate's shove, a
landing at a pad's speed - and on q4ctf5's walkways over space momentum carried
bots over an edge ten times a minute. Each frame the bot looks 0.15 s ahead
along its velocity; with no floor within a survivable 512-unit fall there,
tested with the middle 16 units of the box so a crack is not a void, or a kill
volume, it brakes against its velocity instead. A jump takeoff and a pad or
teleporter entry leave an edge on purpose and are exempt.

World-space movement retains its intended strength while the bot turns to aim.
The projected forward/side command pair is scaled for the player's `CmdScale`,
so a 45-degree aim difference does not reduce speed or air braking. Deliberately
partial input and zero movement retain their strengths.

Combat strafing, teammate separation and unsticking are local hints layered on
top of the route. Each altered direction must pass a player-bounds sweep and a
floor check before it can replace the nav direction, preventing a useful dodge
from becoming a step into a wall or off a ledge. Combat footwork silences the
stuck detector and the goal give-up only while it bends the route materially,
past about 30 degrees: holding range at about the right distance with a light
strafe still walks the route, and progress along it is still the measure. A
stamp on every frame an enemy was merely in view left a bot pressed against a
lip for the whole of a long-range duel, with nothing to free it until line of
sight broke.
Discretionary dodge and recovery hops additionally simulate their flight
through touchdown for both current momentum and a full-speed approach. A
72-unit walking probe does not cover a hop's roughly 220-unit travel. The
bounded proof runs only on fresh grounded jump pulses and refuses unsupported,
lethal or transport landings; deliberate route jumps retain their own proof. A
stranded bot asks for the recovery hop four times a second rather than on every
other grounded frame, and the proof's per-step transport sweeps run only when a
pad or teleporter lies within the flight's reach at all.
An accepted hop holds that proved world-space input through touchdown while
aiming and firing remain independent. A changed goal, strafe or view angle
cannot bend the flight after qualification. A refused launch releases after
128 ms, the flight hold expires after 1,600 ms, and death/spawn clear it. Real
pad launches supersede the hold. A newly acquired speed boost cannot increase
the held command beyond its captured wish speed.

A drop's landing can lie under the lip of its own ledge, so near a drop or a jump
the bot walks the link's own direction until it actually leaves the ground.
A bot that has pressed along its route for a quarter second without closing in
on a walk corner more than a few units above it, or on the pad or teleporter
volume it is stepping into, attempts a hop, on any leg. Ordinary climbs use
the same momentum-aware landing proof and held input as dodge hops; planned
volume entries retain their transport controller. The navmesh steps onto a rise in
one 24-unit stride and the player in five-unit frames, so a bevelled lip - the
sides of q4dm1's pad ramps - can pass the generator and still stop the feet, and
the ramp can stand between a pad's entry node and its trigger; the generic
unstick hop waits for the stuck detector and stays off on the approach to an
action, which is exactly where those ramps are. Entering a pad or a teleporter steers at the volume's own
centre rather than the node the route enters it by. In a pad's flight or a
drop's fall the bot steers with `NavAirSteer` for the velocity that lands it on
the route's node, coasting when no correction is needed, so it comes down where
the navmesh predicted instead of overshooting onto the pad beside a landing.
Pads that throw players at each other over a gap - q4ctf5's either side of
the floating large armour - meet head-on over the void, and solid players
stop each other dead there: within 80 units of touching a pad, a bot brakes
while anyone is flying along that finite corridor in either direction. A
same-direction leader can rebound from the far pad into a follower. Reserving the
expanded trigger boundary does not release that hold; an actual launch does.
A blockage lasting two seconds requests route reconsideration while continuing
to hold, and waiting cannot trigger a climb hop into the pad. A flight that
comes down on the next pad of a chain hands over to it on
touch, and a bot thrown away from its landing at a pad's speed - a short landing
on the pad opposite, a pad the route never planned - coasts rather than brake
the throw over the void, and replans where it sets down. A pad counts as taken only on real launch
speed - more than a jump climbs or a player runs - and its flight ends when the
bot is back on the ground, wherever that is; a flight still airborne after four
seconds is a bounce and releases the route. A
drop or a gap jump taken at speed may land far past its one-cell landing; down
on the landing's floor and ahead of it, the action counts as done only after
the route from the actual touchdown to the next walking corner is checked.
An unsafe continuation brakes and replans. Before a sharp turn into a jump or drop,
the bot sheds sideways momentum while it still has ground to stop on.
It also sheds overspeed before either action. A routed jump is checked again
at the actual takeoff position with its live velocity, rather than assuming
the bot is centred on the entry and already moving along the link. The first
frame uses the encoded movement command, including the jump axis in
`CmdScale`; the flight then uses the same direction-to-landing steering as
the proof. Refused launches brake, can recenter over proven supported ground,
and abandon an unusable approach after the traversal timeout.
Haste and Scout retain their benefit
on ordinary routes, while action entry and flight commands stay within the
base-speed trajectory model. Discretionary combat hops instead qualify their
actual boosted wish speed.

An unplanned pad touch also replaces an ordinary walking route with the pad's
proven natural landing. Detection requires the actual player hull to contact
the force-field brush and a launch impulse, including the first frame when
the grounded flag can still describe the preceding physics update. The
original goal remains available after landing. An overlapping bounding box
alone cannot activate this recovery, and an already planned pad keeps its
chosen landing.
An active pad with no accepted entry node or landing still registers its
activation volume. Its ground nodes have no ordinary exits, preventing a
walking shortcut through a launcher that the route planner cannot safely use.
No destination or recovery flight is invented for that volume. Objective
holding also waits for actual grounding, so crossing a scoring volume in the
air cannot replace an existing landing controller with stationary combat.

Every navigation event is in the `bot_debug 1` log: stuck and off-route
reports with the corner, its travel type and height, completed actions with
their entry, exit and observed landing, and - rate limited - each route snap or
final leg the navmesh refused, with the first test that failed and the entity
that blocked it.

Non-walk links are committed movements rather than loose proximity corners. The
bot first reaches the entry, suppresses combat strafing, generic unsticking and
periodic replans during the action, then advances only after it has left the
entry and reached the destination side. A jump specifically has to leave the
ground and land on the destination floor; being one nearby grid cell below it
does not count. Bounded forward overshoot is accepted so a natural jump need not
stop on an exact sample.

Jump input is pulsed on alternating grounded frames rather than held.
`idPhysics_Player::CheckJump` only fires on a fresh press —
`PMF_JUMP_HELD` blocks a held button and is cleared only on a frame where
`upmove` is released. Grounded pulses provide a fresh edge within one frame
without spraying jump input through the flight.

### Team orders

A human player can order the bots on their own team in team chat, Quake III
style. The line starts with who is addressed - one or more bot names joined by
"and", or a group word (`everyone`, `everybody`, `all`, `team`, `bots`, `guys`,
`squad`) - followed straight away by the order, optionally after `please`:

| Order | Phrases | Effect |
| --- | --- | --- |
| Follow | `follow`, `follow me`, `escort me`, `cover me`, `with me`, `on me`, `come with me`, `regroup` | Escort the speaker in formation, fighting on the way. In every team mode. |
| Defend | `defend`, `defend the base`, `guard`, `defense`, `hold the base`, `stay home`, ... | CTF: hold the team's base. Elsewhere, as below. |
| Defend here | `defend here`, `hold here`, `hold this position`, `guard here`, `stay here`, `camp here` | Hold the area around the nearest spawn point or pickup to where the speaker stands; with nothing fixed within 512 units, follow instead. |
| Attack | `attack`, `offense`, `get the flag`, `grab the flag`, `take the flag`, `capture the flag`, `push` | CTF: take the enemy flag, or escort a team-mate who has it. Elsewhere, free play. |
| Free | `free`, `freelance`, `dismissed`, `roam`, `carry on`, `as you were`, `stop following`, `do your thing` | Back to the bot's own judgement. |

Examples: `kane defend`, `everyone follow me`, `sorg and bagby hold here please`.
Anchoring the order to the names keeps ordinary team talk ("I'll defend", "kane
is following me") from turning into orders. Only humans give orders, only in
team chat, and only to their own team. Up to two of the bots ordered
acknowledge in team chat from their character's `order` lines, which skip the
chattiness roll but respect the flood throttle; orders themselves work with
`bot_chat 0`.

An order outranks the role the allocator would have handed out, but not the
rules' urgent steps: a carrier still takes the flag home, and anyone may still
grab health at the edge of death. Bots under orders are left out of role
allocation, so their team-mates share the remaining roles around them. An order
lasts until it is replaced or dismissed, the bot changes team, or the map
changes; a follow order lapses while its leader is dead and ends when the
leader leaves or switches team. Only an order that means something right now
takes a bot out of the allocation: an attack order outside CTF, or a follow
order while its leader is dead, leaves the bot to the allocator like any other
team-mate. It used to take the bot out of every role for as long as the order
stood, so "everyone attack" in Freeze Tag stopped every rescue for the rest of
the map and a bot told to follow had no objective at all while its leader was
dead.

## Characters

`src/mpgame/bots/BotCharacter.{h,cpp}`. Difficulty used to be five numbers on a
straight line, which made every bot at a given `bot_skill` identical and every
match feel the same. It is now three layers of content that resolve, once per
spawn, into one flat `botTraits_t` — 51 floats that are the only thing the
per-frame combat code reads:

1. **Skill level, 1 to 5.** The baseline curve. Every trait has a value at every
   level, and the fractional levels `bot_skillVariance` produces are interpolated
   between the two bracketing rows. This layer on its own is a complete, playable
   bot, and it is exactly what `bot_characters 0` gives you.
2. **Play style.** A named archetype that biases the baseline. A style says what
   a bot *wants to do*, never *how well it does it*: a skill 1 sniper and a
   skill 5 sniper both hold range and both reach for the rail gun, and only one
   of them hits with it. Six ship — `rusher`, `sniper`, `roamer`, `hunter`,
   `ambusher`, `skirmisher`.
3. **Character.** Identity: display name, player model, the skill band it is
   picked for, and its own per-trait bias and per-skill-level overrides. Its
   voice is a separate chat bank joined to that identity by character name.

Layers 2 and 3 do not restate the trait list. They carry sparse statements —
`set`, `add`, `scale` — that name a trait by the same word the struct uses, and
that resolve through one static field table. Adding a new trait is one row in
that table and one number per skill level in the baseline curve; no parser and
no content file has to learn it exists.

Resolution order matters, because `scale` composes with whatever came before it:

```
baseline( skill level, after variance )
  -> style body
  -> style skill <n> { } block
  -> character body
  -> character skill <n> { } block
```

Each statement is applied in file order and its field is clamped to the table's
range immediately afterwards, so a typo in a content file cannot produce a bot
that turns at 90000 degrees a second.

Weapon preferences are the one thing not reachable through the field table.
They are named entries rather than scalars, and they *merge* rather than
overwrite, so a character with an opinion about one weapon keeps the rest of its
style's opinions intact.

### What each skill level feels like

The point of the curve is that the levels differ in kind, not only in degree.

- **1** is genuinely poor and should be beatable by someone who has never played
  the map. It notices you inside a 130 degree cone at 1200 units, takes about
  390 ± 150 ms to react (a raw 460 ± 180 shortened by its eagerness, below),
  and pays all of that again every time you break line of sight. Its aim
  carries only half of your motion forward and trails the rest by 0.4 seconds,
  carries 4.2 degrees of wandering tremor, and swings
  at 170 deg/s on an underdamped slew that still overshoots and hunts. Its 0.80
  initiative turns the raw 14 degree / 240 ms trigger values into an effective
  18.2 degree cone and 96 ms settle instead of waiting so long for bad aim to
  settle that it barely fires; the extra attacks remain inaccurate. It barely
  leads a projectile and gets roughly one combat decision in two wrong.
- **2** is a distracted human. It notices you at 1500 units inside a 139
  degree cone, reacts in about 0.37 s, tracks better, still overshoots, and
  still sprays. Its row sits 60 % of the way from skill 1's to where it used
  to be: as authored, skill 2 stood a much bigger step above skill 1 than any
  other level above the one below it (see the levelling evidence under
  Testing).
- **3** is the default and is meant to read as a competent regular: about 310 ms
  on a fresh sighting, 215 ms on a re-peek, 2.1 degrees of tremor, a 7 degree
  effective cone it settles into for about 140 ms before shooting, and
  projectiles led at about 60 % correctness.
- **4** punishes standing still. It notices you at 2600 units across a 170
  degree cone, reacts in 220 ms, holds a 4 degree cone, tracks 92 % of your
  motion, and leads well enough that running in a straight line at range is
  fatal.
- **5** is sharp but deliberately not an aimbot: 140 ± 45 ms reaction that it
  still re-pays at 40 % on a re-peek, 97 % of your motion tracked with a
  0.08 s lag on the rest, a reversal trailed for about 75 ms, half a degree of
  tremor it can never switch off, a 2 degree cone it refuses to shoot outside
  of, and 97 % lead correctness with a 5 % random error. A steady strafe at
  mid range is hit about half the time; unpredictable direction changes are
  what still beat it.

Awareness climbs with the same curve. `hearing` scales how far a bot hears
fire, footsteps and pickups - 45% of the nominal range at skill 1, all of it at
skill 5 - and `itemTiming` is how well it keeps a respawn timer it witnessed,
from barely at all at skill 1 (0.05) to within a few percent at skill 5 (0.92).

The trigger duty cycle of an automatic weapon is long at both ends, for
opposite reasons. Skill 1 holds the trigger down about 81 % of an engagement
whether or not it is on target - spray and pray, not a nerfed rate of fire. The
middle levels tap (skill 3 about 78 %). Skills 4 and 5 hold it for 86 % and
93 % of the time they are on target, as a strong player does: the cone gate
ends a burst the moment the aim leaves, and every pause costs the rest and a
fresh settle before the next round. Before 1 November skill 5 tapped 140-300 ms
bursts with 80 ms rests, about 60 % of its time on target once the settle is
counted.

`initiative` is taste, so it is allowed to bend competence only a little. It
scales the reaction by a quarter at most (0.75 at 1.0, 1.25 at 0.0), widens the
fire cone by up to half, and shortens the settle, but never below a quarter of
the authored value. Before those bounds a style's `add initiative 0.30` on the
skill-1 baseline of 0.80 reached 1.0, halved the reaction and set the settle to
zero: a "genuinely poor" rusher fired the frame its cone was entered and reacted
faster than a plain skill-4 bot.

`combatRange`, `aggression`, `patience`, seven of the tactical-personality
traits, and `chatiness` are deliberately flat across the whole curve. They are
taste, not competence, and a skill curve that moved them would make every
high-skill bot play identically, which is the thing this system exists to
prevent. `initiative` and `suppressionMsec` are the two exceptions: novices
take imperfect shots and waste rounds through recently crossed doorways, while
high-skill bots default to waiting for a visible, settled shot.

### The aim model

Skill is legible in play because it is spent on the *shape* of the aim rather
than on a hidden accuracy roll.

- **Belief.** The bot aims at where it *thinks* you are. Each frame the belief
  first moves on by `aimAnticipation` of the velocity the bot has registered
  for you - 0.50 at skill 1, 0.97 at skill 5 - and a first-order lag then pulls
  it toward where you actually are, with a time constant of 0.4 s at skill 1
  and 0.08 s at skill 5. The registered velocity follows your real one with a
  time constant of 55% of the bot's reaction time (about 75 ms at skill 5,
  250 ms at skill 1), so a change of direction is trailed for a moment at every
  skill: that is the miss a juking player earns, and against a steady runner
  only the unanticipated share of the motion trails. A lag alone trails a
  steady runner by speed times the time constant for as long as the run lasts:
  26 units at skill 5 and 320 units a second, off a 32-unit hull, so before
  1 November no bot could keep its aim on anyone running in a straight line,
  and the better the bot the more surely it missed. On a fresh
  acquisition the belief snaps to the truth with no registered motion, so an
  engagement never opens with the bot aiming at the last fight. Out of
  sight, the belief coasts on the last seen velocity for a moment, at no more
  than the speed it saw: `pursuit` runs to 2 for how long a chase lasts, and as
  a gain on that extrapolation it stops at 1.
- **Lead.** Only for weapons that actually launch something. The test is
  whether the weapon declares `def_projectile`, not the hitscan flag, because
  that flag is false for the gauntlet and the lightning gun even though both are
  instant-hit. Projectile speed and gravity are resolved through the entity def
  system so the multiplayer retune applies (rocket 935, not 900). Three short
  flight-time iterations combine target velocity, the bot's own velocity and
  gravity compensation, so rockets, nails, grenades and napalm receive the same
  physically consistent lead instead of treating ballistic weapons as linear.
  The target velocity is the registered one, so a rocket fired just after a
  reversal goes where the target was going.
- **Tracking error.** A lag *across* the view rather than noise: the faster you
  cross the bot's screen, the further behind its aim sits.
- **Tremor.** Two slow sine octaves, phase-seeded from the client number so no
  two bots shake in step. Continuous and smooth, so it reads as an unsteady hand
  rather than as white noise, which is what the old 400 ms step offset read as.
- **Turn.** A damped second-order slew driven by `turnSpeed`, `turnAccel` and
  `turnDamping`. Under-damping at low skill is what produces the visible
  overshoot-and-hunt; skill 5 settles cleanly. In a fight the damping acts on
  the view's speed relative to the aim point's angular speed, which is smooth
  pursuit: the view follows a point crossing it and corrects around that.
  Damped against standing still, as it was before 1 November, the slew trailed
  anything crossing the view by `2 x turnDamping / natural frequency` seconds
  of its motion - 0.22 s at skill 5, two body widths on a strafer at 600 units.
  A point that jumps by more than 720 degrees a second in one frame (a new
  target, a new route corner) is not pursued. With nobody to shoot at, the
  view slews as before.
- **Reaction.** Computed once per acquisition, never per frame, and it now
  includes a peripheral penalty scaled by how far off centre you were when the
  bot first saw you. A re-acquisition inside `reacquireMsec` still pays
  `reacquireFraction` of the full cost. That fraction is the fix for the old
  zero-reaction bug, in which a bot re-peeking a corner had already paid its
  reaction once and fired instantly forever after — and it is why the skill 5
  reaction time went *up*, from 70 ms to 140 ms.
- **Trigger.** The aim has to sit inside the fire cone for `aimSettleMsec` and
  the view has to be slewing slower than `holdFireTurnRate` before the trigger
  is released. The cone is measured against the same point the bot is aiming at,
  including the lead — a gate that tests the target centre while the aim sits on
  the lead point produces a bot that aims correctly and then refuses to shoot.
  The slew is measured relative to the aim point's own angular speed: a view
  tracking a strafer 80 units away turns at about 230 degrees a second while
  sitting on it, and the absolute gate refused exactly those shots.
- **Mistakes.** Rolled on acquisition and periodically while engaged. One of:
  lose tracking, mistime a shot, keep the wrong weapon, dodge late. A mistake is
  a bounded, readable failure rather than a silent accuracy penalty.

`bot_debugAim 1` logs the reaction, effective cone, settle, lead and whether a
shot is suppression fire, which is how the curve is tuned.

### Tactical personality traits

The skill curve controls execution while these traits control intent. Seven
stay flat and styles or characters move them independently. `initiative` and
`suppressionMsec` also make the low-skill baseline more willing to shoot:

| Trait | Range | Behavior |
| --- | --- | --- |
| `initiative` | 0..1 | How readily the bot accepts an imperfect shot. Higher values shorten reaction and settle time, widen its effective fire cone, and tolerate a faster view slew without improving where the aim points. Baseline skill 1 is 0.80; skills 4-5 are 0.50. |
| `targetStickiness` | 0..1 | How much better a new opponent must be before the bot abandons its current target. |
| `opportunism` | 0..1 | How strongly wounded opponents are preferred during target selection. |
| `vengefulness` | 0..1 | How strongly the bot favors the opponent who last killed it. |
| `suppressionMsec` | 0..2000 | How long it keeps shooting at the last believed position after an opponent crosses cover. The plain curve falls from 1200 ms at skill 1 to zero at skills 4-5. |
| `strafeRhythmMsec` | 100..3000 | Base time between changes of combat strafe direction. |
| `strafeRhythmVarianceMsec` | 0..3000 | Random spread added to that strafe rhythm, from clockwork movement to irregular footwork. |
| `weaponSwitchMsec` | 100..3000 | How often the bot reconsiders its weapon for the current range. |
| `aimHeight` | -0.4..0.4 | Personal vertical bias within the target bounds: low body, centre mass, or high chest. Skill accuracy still applies around it. |

These make profiles tactically legible. Gunner and Rhodes suppress doorways,
Sorg changes direction and weapons rapidly, Cortez holds one target and aims
high, Bagby fires readily but is slow to correct a poor weapon choice, and
Makron develops a persistent grudge.

### Styles

| Style | Wants to |
| --- | --- |
| `rusher` | Close to 220 units and stay there. Aggressive, impatient, short-range weapons, a wider cone because it is always moving. |
| `sniper` | Hold 1400 units and trade with hitscan. Patient, still, rail gun first, and useless in a corridor - which is the point. |
| `roamer` | Play the map. Near baseline everywhere, `itemFocus` up 40 %, fights what it meets on the way to the next item. |
| `hunter` | Pick one opponent and keep going. Long pursuit and a long re-acquisition window, so it holds a target through cover; barely interested in items. |
| `ambusher` | Sit on a choke point and wait. Very patient, cheap peripheral reaction and a quick trigger because it is already pre-aimed, and it dodges little. |
| `skirmisher` | Trade and leave. High mobility, jumps often, breaks off early, short bursts. |

### The roster

Sixteen characters ship, reusing the existing bot name pool so the cast still
looks like Quake 4. Each has a skill band, and `PickCharacter` prefers an unused
character whose band contains the current skill — falling back to the band, then
to anyone at all, because a full roster must never stop a bot being added.

| Character | Style | Band | Combat identity | Voice |
| --- | --- | --- | --- | --- |
| Voss | roamer | 3-5 | controls useful ground, protects his health and chooses threats carefully | calm field authority; disciplined, protective orders |
| Cortez | sniper | 3-5 | holds long angles, relocates deliberately and favors economical rail shots | courteous, composed sharpshooter with quiet confidence |
| Bidwell | rusher | 3-5 | applies disciplined forward pressure without throwing away his life | gruff, profane sergeant with little patience |
| Rhodes | ambusher | 3-5 | controls weak points with predictive rockets and grenades | warm Texas demolition swagger and craftsman's pride |
| Sledge | rusher | 4-5 | advances under deliberate mid-close heavy-weapons pressure | thoughtful, formal and dryly understated |
| Morris | roamer | 3-5 | moves aggressively through the rotation and decides quickly | fast, crude commentary backed by real competence |
| Strauss | ambusher | 2-4 | solves prepared lanes, guards equipment and retreats early | precise, vain technician whose temper breaks through |
| Tetzlaff | sniper | 1-3 | fixates on static rail shots but reads moving fights poorly | self-promoting range ego with an excuse for everything |
| Marsh | roamer | 2-4 | flexible generalist who takes useful fights without tunnel vision | warm, observant competitor who respects good play |
| Hollenbeck | roamer | 2-4 | controls resources, consolidates gains and abandons wasteful chases | practical field officer who turns setbacks into orders |
| Sorg | skirmisher | 2-4 | reads peripheral movement and rotates through evasive routes | restless pathfinder always reading the next opening |
| Anderson | skirmisher | 1-3 | mobile, item-aware support fighter who breaks off to survive | reassuring young medic, proactive under pressure |
| Gunner | rusher | 3-5 | maintains nailgun and grenade suppression while pursuing targets | translated Strogg combat reports: suppress and advance |
| Makron | hunter | 5-5 | identifies the strongest threat and pursues it relentlessly | imperious ruler who issues threats, not conversation |
| Kane | skirmisher | 4-5 | controlled mid-range survivor with unusually steady movement | sparse, plain human tactical speech |
| Bagby | roamer | 1-2 | nervous logistics runner who fires readily, aims low and is slow to correct a poor weapon choice | chatty, candid novice who learns out loud |

Band coverage is intentionally broad rather than even: levels 1 through 5 have
3, 7, 12, 12 and 9 preferred characters respectively. Anderson keeps a canon
human in the level-1 pool, while Makron remains exclusive to level 5. The band
only controls roster preference; the resolved baseline still determines how
well every selected character sees, aims, reacts and decides.

## The file format

Styles, character mechanics and character voices are plain text in brace
blocks, read with `idLexer` under `DECL_LEXER_FLAGS`. They are deliberately
*not* decl types: this needed no engine change, and those flags carry
`LEXFL_NOFATALERRORS`, so a malformed content file warns and is skipped instead
of killing a server.

```
content/baseoq4/pak0/botfiles/styles/<style>.style
content/baseoq4/pak0/botfiles/characters/<name>.bot
content/baseoq4/pak0/botfiles/chats/<name>.chat
```

These subdirectories and extensions are independent of the pre-existing Quake 3
format prototypes in `botfiles/bots/*.c`, `botfiles/items.c` and
`botfiles/weapons.c`. Nothing has ever parsed those old files, and the new
readers can never pick them up. Files are enumerated through
`fileSystem->ListFiles`, so a mod or a pk4 can add or replace styles, characters
or voice banks without a build step; anything dropped under
`content/baseoq4/pak0` is packed into `pak0.pk4` automatically.

The grammar:

```
style "<name>" {
    description  "<text>"
    inherit      "<other style>"        // optional; resolved after every file is read

    set    <trait> <number>             // trait  = number
    add    <trait> <number>             // trait += number
    scale  <trait> <number>             // trait *= number

    weapon "<weapon class>" <bias>      // 1.0 neutral, 0.0 last resort

    skill <1..5> { <same statements> }  // layered after this file's body
}

character "<display name>" {
    description  "<text>"
    inherit      "<style>"
    skillBand    <min> <max>
    model        "<playerModel decl>"   // non-team games
    modelMarine  "<playerModel decl>"   // team games, marine side
    modelStrogg  "<playerModel decl>"   // team games, strogg side

    set/add/scale <trait> <number>
    weapon "<weapon class>" <bias>
    skill <n> { ... }
}

characterChat "<display name>" {
    chat <event> {
        "line"
        "line"
    }

    reply <rule name> {
        priority  <0..100>
        source    any | player | bot
        addressed either | required | forbidden

        trigger "<word or phrase>"
        trigger "<another phrase>"

        "response line"
        "another response"
    }
}
```

Nothing in a content file is allowed to be fatal. An unknown key warns and is
skipped, and so does an unknown trait name or an out-of-range `skill <n>`. An
unresolved `inherit` warns and leaves that layer empty. A `characterChat` owner
is matched to the loaded character name case-insensitively; an owner with no
matching character warns and is ignored rather than creating a half-character.
The shipped `.bot` files contain mechanics and models only, and every shipped
voice line lives under `botfiles/chats`.

Two chat errors are rejected at *load* rather than silently vanishing later: a
line longer than `BOT_CHAT_MAX_LEN` (160 characters, because the broadcast path
drops the whole line at 240 once the decorated name is added), and a line
beginning with `#`, because chat text is passed through
`common->GetLocalizedString` and `#str_` would be substituted out from under the
author.

A style, abbreviated from the shipped `sniper.style`:

```
style "sniper" {
	description	"holds range and trades with hitscan"

	set	combatRange		1400
	set	aggression		0.28
	set	patience		0.85

	// Takes its time and will not shoot at anything it is not sure of.
	scale	aimSettleMsec		1.30
	scale	fireConeDeg		0.70

	weapon	"weapon_railgun"	2.00
	weapon	"weapon_shotgun"	0.35

	// A bad sniper is worse than a bad anything else: it stands still at
	// range and cannot hit.  Give the low levels back some willingness to move.
	skill 1 {
		scale	strafeChance	1.50
		scale	combatRange	0.75
	}
}
```

Character mechanics, abbreviated from `voss.bot`, contain no dialogue:

```
character "Voss" {
	inherit		"roamer"
	description	"calm field leader who values discipline over heroics"

	skillBand	3 5

	model		"model_player_marine_voss"
	modelMarine	"model_player_marine_voss"
	modelStrogg	"model_player_kane_strogg"

	// Holds useful ground, protects his health and will not turn one kill into
	// an undisciplined chase.
	set	combatRange		720
	scale	aggression		0.84
	add	patience		0.15
	scale	pursuit			0.75
	add	retreatHealth		0.08

	// His edge is judgment, not superhuman aim.
	scale	targetSelection		1.06

	// Talks rarely, but an order arrives while it can still matter.
	set	chatiness		0.30
	scale	chatDelayScale		0.90
}
```

The corresponding voice bank is `botfiles/chats/voss.chat`:

```
characterChat "Voss" {
	chat kill {
		"$other overcommitted. Don't do the same."
		"I had the angle."
	}
	chat death {
		"Good shot. I gave you the angle."
	}
}
```

## Chat

Chat is event driven and entirely server-side. A line goes out through
`idMultiplayerGame::ProcessChatMessage` with the bot's own client number, which
is the same call the server makes for a human's `say`, so the result is
indistinguishable from a player typing. Routing it through the `say` command
instead would make every bot speak as the host, and hooking the obituary would
make bots silent on a dedicated server, because that path's local branch never
runs there.

Dialogue is character-owned without being mixed into the mechanics file. Each
`botfiles/chats/*.chat` file names its owner in a
`characterChat "<display name>"` header. Chat banks load after the character
definitions and merge by that name case-insensitively, so the filename is a
useful convention rather than the identity key.

The events are: `entergame`, `levelstart`, `kill`, `killGauntlet`,
`killStreak`, `revenge`, `death`, `deathAccident`, `itemDenied`, `leadTaken`,
`leadLost`, `matchWin`, `matchLose`, `farewell`, `order`. A character with no
lines for an event simply says nothing — silence is a valid answer and nothing
invents a fallback line. `order` is the acknowledgment of a team order (see
"Team orders"); it goes to team chat, skips the chattiness roll so the speaker
knows the order landed, and still respects the flood throttle.

Every shipped character carries eight alternatives for each event: 120 lines
per character and 1,920 across the roster. This is a content target, not a
parser requirement; add-on characters may use smaller banks or intentional
silence. The uniform depth keeps losses, lead changes and departures from
becoming more repetitive than kills.

### Triggered replies

The same voice file may also contain `reply` rules. They let a bot answer
recognisable ideas in typed player chat and ordinary event-driven bot chat
without putting dialogue or phrase lists in C++. The shipped rules cover
requests for help, post-match sportsmanship, challenges, greetings, thanks,
praise, apologies and farewells. A ninth low-priority rule lets each character
answer when addressed by name. These are deliberately broad conversational
signals, not a general language model.

Incoming text is stripped of Quake 4 colour escapes, folded to lower-case and
split at punctuation. Triggers are normalised the same way and match only
contiguous whole words: `hi` matches `Hi, Voss`, but never the `hi` inside
`this`. Rules first filter on `source` and `addressed`; the highest `priority`
then wins, followed by the longest matching phrase; file order breaks a
remaining tie. The selected response cannot immediately repeat. A directly
named bot is preferred as the responder. Otherwise at most one eligible bot
answers, so one greeting cannot make the whole server speak.

`source` accepts `any`, `player` or `bot`. `addressed` accepts `either`,
`required` or `forbidden`; a bot is addressed when its display name appears as
whole words in the message. Each character may contain at most 32 reply rules,
with up to 32 triggers and 32 responses in each. Triggers are limited to 64
characters after normalisation. Priorities are clamped to
0 through 100. Invalid values, oversized rules and unknown keys warn and remain
contained to the affected setting, trigger, line or rule rather than preventing
the server from starting.

Replies use only `$self`, `$other` and `$map`: the speaker and map are always
known, while `$other` is filled from the server's trusted player name rather
than from chat-packet display text. Unknown or unavailable reply tokens are
rejected when the bank loads.

Responses keep all normal chat safeguards. A bot with a line already queued
does not lose it to a reply. Replies use the character's typing delay and the
existing per-bot and global throttles, plus a per-speaker cooldown that prevents
one chatter from rotating through the whole roster. Unaddressed bot-to-bot
responses are intentionally much rarer than replies to people. A generated
reply is explicitly marked in the send path and cannot trigger another reply;
voice macros are excluded too, so acknowledgement loops cannot form.

Global chat may choose any bot. Team chat can choose only a bot on the
speaker's team and the response remains team-only. Spectator-only chat, server
console text and malformed or rejected messages never reach reply matching.

Each of the fifteen normally talkative characters ships four alternatives for
each of the nine reply rules. Kane retains two terse alternatives per rule.
That adds 558 reactive lines to the 1,920 event lines, for 2,478 authored lines
across the roster.

Lines may use `$self`, `$other`, `$weapon`, `$map` and `$item`. A line requiring
a token that is unavailable for its event is skipped, preventing a partial or
mangled sentence from reaching chat. `$map` is supplied for every event;
opponent and weapon names come from combat events, `$item` is specific to
`itemDenied`, and in `order` `$other` is the player who gave it. Match-end events do not identify the winner or loser as `$other`.
Selection is uniform over the usable lines excluding whichever was used last,
so a line never repeats back to back.

The send delay and throttles keep the chatter believable and safe:

- A bot never speaks on the frame of the event. It pauses for `bot_chatDelay`,
  then waits for the visible line length at `bot_chatCPM` characters per
  minute. Formatting escapes do not count as typed characters. The whole delay
  is scaled by the bot's `chatDelayScale`, which is worse at low skill, given a
  little jitter, and capped at five seconds so even a long line remains brief.
  During that interval Quake 4's stock chat icon appears above the bot just as
  it does above a human player composing a message; no replacement material is
  required.
- Throttles, per bot and server wide. This is not politeness: **the engine has
  no chat flood protection anywhere**, and `idAsyncServer::SendReliableMessage`
  drops any client whose reliable queue overflows, so unbounded bot chatter can
  kick real players off a server. `bot_chat 2` halves both throttles and makes
  every bot more talkative; `bot_chat 0` silences them completely.
- One line at a time. A bot already typing keeps that line rather than dropping
  it for a newer event. At `bot_chat 2` the per-bot throttle is shorter than the
  longest typing delay, and a frag a few seconds after a death used to overwrite
  the death line after its throttle stamp had already been spent: one
  reservation, no line.

Team chat is only ever used in a team game. `ProcessChatMessage` does not check
that itself, and `team` set in a deathmatch colours everyone as Marine and
restricts delivery by the team field.

## Adding a character

1. Write `content/baseoq4/pak0/botfiles/characters/<name>.bot`. Start from an
   existing one; the only required keys are the `character "<name>"` header and
   an `inherit` naming a style that exists. Keep this file to models, selection
   metadata and play-style modifiers.
2. Set `skillBand` to the range the character is meant to be picked for. A
   character out of band is only used as a last resort, so a band of `5 5` means
   "this one shows up when the server is set to hard".
3. If it names a model, the name must be a real `playerModel` decl, and in a
   team game its decl team has to match the side it spawns on — hence the
   separate `modelMarine` and `modelStrogg` keys. `model_player_tactical_elite`,
   `model_player_tactical_command` and `model_player_marine_tech` declare no
   team and are legal on either side. An unknown model name makes the player
   rewrite its own user info every update.
4. Bias only what makes the character distinct. The style already covers how it
   plays and the skill curve already covers how well; a `.bot` file that sets
   twenty traits is fighting both.
5. Write `content/baseoq4/pak0/botfiles/chats/<name>.chat` with a
   `characterChat "<name>"` header. The owner name is matched
   case-insensitively to the character header; matching filenames keep the pair
   easy to find but are not used for ownership.
6. Add lines for as many events as the voice supports. Keep them short, in
   character and inoffensive. The shipped roster uses eight alternatives for
   all fourteen events; add-ons may deliberately use fewer or stay silent. Add
   `reply` rules when the voice should recognise typed phrases. Prefer
   whole-word phrases over single common words, use `addressed required` for a
   name-only fallback, and make every response sensible without knowing more
   than the rule's conversational intent.
7. `ninja -C builddir pak0.pk4` to repack, or run from a loose `fs_devpath`
   tree, then `botreload` in the console. It rereads styles, character mechanics
   and chat banks without a restart or map change.

## Commands

| Command | Effect |
| --- | --- |
| `addbot [name] [skill] [exact]` | Add one bot. Picks an unused name and character if none is given. The optional skill is a per-bot override that bypasses `bot_skill` and `bot_skillVariance`; `exact` requires the named character and is intended for authored matches. Builds the navmesh on first use. |
| `removebot [name]` | Remove one bot, by name or the last one added. |
| `kickbots` | Remove every bot. |
| `botlist [weapons]` | List the bots with their character, style and effective skill, the match statistics behind how well each one fights (kills, deaths, hits and shots over every weapon, accuracy and damage given since the map started), and the state of the navmesh. `weapons` adds a line per weapon each bot has used with its hits, shots and kills. |
| `botcharacters` | List every loaded character with its style, skill band and which bot currently has it. |
| `botreload` | Re-read style, character and chat files without a map change. Bots keep the character they are wearing where the name survives. |
| `navmesh build` | Rebuild the navmesh, picking up a changed `bot_navCellSize`. |
| `navmesh info` | Report node, link and build-time counts. |
| `navmesh probe <x> <y> <z> [radius]` | Print every node within twice the radius (default 24) of a point, with its links and the support verdicts behind the graph, then height maps of the floor around the point as the agent hull, the support box and a point each find it, four units apart. How the rim and lip cases in this record were read. |

## Cvars

| Cvar | Default | Effect |
| --- | --- | --- |
| `bot_enable` | `1` | Allow bots to be added at all. |
| `bot_minPlayers` | `0` | Top the match up to this many players with bots. `0` disables. |
| `bot_skill` | `3` | 1 (harmless) to 5 (unpleasant). Selects the baseline row every trait is read from - vision, reaction, aim steadiness, trigger discipline, decision quality. See "What each skill level feels like". |
| `bot_skillVariance` | `0` | Spread skill up to this many levels either side of `bot_skill`, per bot, so a match is not all one difficulty. The roll is seeded from the client number, so it is stable for the life of that bot. A roll that would leave the 1..5 range is reflected back in rather than clamped, so a spread around skill 1 is a spread and not half a roster at exactly 1.0. |
| `bot_characters` | `1` | Use the character files. `0` leaves every bot on the plain skill curve with a name from the pool and nothing to say. |
| `bot_forceCharacter` | `""` | Put every bot on this one character, by name. A tuning knob, deliberately not archived - a server that saved it would field a roster of clones. |
| `bot_chat` | `1` | `0` silent, `1` normal, `2` chatty and with chat throttles halved. Applies to event lines and triggered replies. |
| `bot_chatDelay` | `600` | Initial thinking pause before the length-based typing delay, in milliseconds. |
| `bot_chatCPM` | `900` | Base typing speed in visible characters per minute. Character delay scales and a five-second cap keep the result varied but brief. |
| `bot_debug` | `0` | `1` logs navigation events, `2` adds a periodic per-bot status line. |
| `bot_debugNav` | `0` | `1` draws the navmesh near the local player, `2` adds each bot's current route. |
| `bot_debugAim` | `0` | Log the reaction, settle and lead values behind every shot. |
| `bot_profile` | `0` | Every N seconds print `botprofile:` costs (average/worst think, searches/failures, expansions and pricing/navigation time) plus `botprofile-percentiles:` conservative p50/p95/p99 think-time ceilings from a bounded quarter-millisecond histogram. |
| `bot_navCellSize` | `24` | Sampling resolution in world units. Smaller finds more ground and costs more to build. |
| `bot_pause` | `0` | Freeze all bot input. |

Bots are server-side only. Everything a server operator is expected to set is
archived - `bot_enable`, `bot_minPlayers`, `bot_skill`, `bot_skillVariance`,
`bot_characters`, `bot_chat`, `bot_chatDelay` and `bot_chatCPM` - so a dedicated
server config can set them once. That is not cosmetic: a command-line `+set`
never reaches a `CVAR_GAME` cvar, because the game module registers it after
the engine has parsed the command line, so only archived values survive.

## The create-server menu

`bot_minPlayers` and `bot_skill` are the two settings a listen-server host is
expected to touch, so both are rows in **Advanced Server Options** on the
multiplayer create-server screen - `pop_createAdv_botFill_val` and
`pop_createAdv_botSkill_val` in `guis/mainmenu.gui`. Both are `choiceType 1`
widgets, which write the raw string from their `values` list rather than an
index, so the list has to stay inside the range the cvar declares.

That menu is reachable while `game_sp` is loaded - the module is not swapped
until `spawnServer` - and `idChoiceWindow::InitVars` resolves its cvar exactly
once, when the gui is parsed. A `CVAR_GAME` cvar the loaded module never
declared is simply absent, and the widget warns and then does nothing for the
rest of the session. `Session_DeclareMultiplayerMenuGameCVars` in
`src/framework/Session.cpp` therefore declares both, with `game_mp`'s own
defaults, immediately before the main menu gui is loaded. `idInternalCVar::Update`
keeps the current value when a static declaration arrives later and only adopts
its reset value and range, so the module swap at `spawnServer` inherits whatever
the host picked. Anything else added to that popup that lives in `game_mp` needs
the same treatment; `tools/tests/mp_bot_server_menu.py` pins it.

## Arena Campaign orchestration

The single-player Arena Campaign drives the multiplayer bot runtime through a
private local match. Its authored ladder is
`content/baseoq4/pak0/arena/openq4_campaign.cfg`: a versioned, lexer-friendly
manifest containing five tiers of four matches. Each match names a stock map,
one combat game type, its mode limits, a base skill, and an explicit character
roster with per-bot skill offsets.

The roster is intentionally explicit instead of using `bot_minPlayers`.
Campaign matches must be repeatable: the same card always produces the same
characters and effective skills, while the ordinary automatic-fill behavior
remains available for multiplayer servers. Effective skill is the tier's base
skill plus the campaign difficulty adjustment and character offset, clamped to
the normal 1-to-5 bot range. Team matches list five bots so the local player and
roster can form balanced three-player sides.

Progression follows the Quake 3-style ladder contract. Match indexes 0 through
2 begin unlocked inside an available tier. Winning all three unlocks index 3,
the tier boss. A boss win unlocks the next tier, and the final boss completes
the campaign. Losses never remove wins or locks; reset is a separate confirmed
menu action. Arena progress is independent of Mission savegames and ordinary
server configuration.

Only kill-driven modes appear in the shipped ladder: Duel, Deathmatch, Team
Deathmatch, Red Rover, and Clan Arena. All selected maps advertise DM or Team
DM in the stock map declarations, which also supplies the layout for openQ4's
derived modes. This is an authored campaign-scope choice, not a general bot
limitation: ordinary multiplayer bots now pursue Standard CTF, One Flag, Arena
CTF, Arena One Flag, Freeze Tag rescues and DeadZone play. Adding one of those
to Arena still requires a deliberate card, roster, limit, progression and
result-flow pass for that mode.

## Known limits

- Multiplayer has no ladders to model. `idPhysics_Player` checks for ladder
  surfaces only outside multiplayer, as retail Quake 4 did ("no ladders in
  MP"), so a ladder brush in a custom map is not climbable by players either
  and a bot route needs no ladder link. This is a deliberate retail behaviour,
  not a navigation gap.
- Lifts are not modelled. Movers are floor where they stand when the navmesh is
  built; route validation re-sweeps a chosen route against moving solids, which
  catches a mover in the way but not one that has moved out from under a node.
  No stock multiplayer map routes through a lift, so a custom map that needs one
  to connect its levels leaves those levels unreachable to bots.
- The gap-jump scan used to stop at the first floor within reach in each
  direction, so a trench no deeper than 128 units got only the drop into it.
  It now links that drop and keeps scanning for a far rim at the takeoff's
  level (`GapJumpClear` still proves the whole arc); the build summary counts
  these as "gap jumps (N over trenches)". Twenty-four of the 33 catalog
  variants gain such jumps ([testing](#team-knock-guard-and-trench-rims-23-october-2026)).
- A bot below a fall-damage tier qualifies each pad landing on a selected route
  by flying the pad again, which is up to several hundred box sweeps per pad
  link per search. It is paid only while the bot is that hurt, which is also
  when it is heading for health rather than crossing the map.
- Role allocation compares landmark route-cost lower bounds rather than full
  routes, with a margin for bots already filling a role. It can mis-rank runs
  where those bounds are loose, and falls back to straight-line distance when
  a target or player has no usable navigation node. Teammate pickup claims
  count existing commitments and reduce utility; they do not rank travel times.
- `FindNearestNode` spends at most `NAV_MAX_WALKABLE_PROBES` collision probes
  when asked to prove walkability. A bot standing somewhere that genuinely needs
  more than that to escape falls back to wandering rather than stalling the
  server frame.
- Generation past `NAV_MAX_NODES` stops adding ground but still finishes linking
  what it has, so the graph is smaller rather than broken. A cell size fine
  enough to trip the limit still leaves part of the map unnavigable, and the
  warning says so.

- Deliberate objective play currently covers Standard CTF, One Flag, Arena CTF,
  Arena One Flag, Freeze Tag rescue and DeadZone. Attack & Defend, Overload,
  Harvester and Domination have descriptors or borrowed layouts but not the
  complete authoritative runtime state and scoring rules required for honest
  objective decisions. Their objective query therefore fails closed and bots
  deliberately fall back to combat, inventory pickups and directed roaming.
- A pad's landing is simulated with the same air steering the bot flies with,
  but at a fixed frame step and without other players in the way, so a flight
  can still come down somewhat off its node. It ends when the bot is back on
  the ground, and off-route recovery re-routes from wherever it landed.
- Enemy knockback and in-flight player collisions can still carry a bot into
  a void. Corridor occupancy reduces preventable launches, but does not
  arbitrate simultaneous grounded entries at opposite ends. The health guard
  prices each landing using current health and armour; damage received after
  takeoff can still make that committed landing fatal.
- Routed jump qualification conservatively refuses unsettled inward ground
  velocity instead of approximating the player's slope-slide adjustment.
  Command-byte rounding and changing speed powerups remain small differences
  from the fixed-step flight model. A failed live launch may require a new
  approach; the bot does not construct a new run-up while standing on the lip.
- The navmesh is rebuilt at every map load rather than cached on disk.
  Generation runs once per map and is lazy, so a server without bots never pays
  it.
- `bot_debugNav` uses the renderer's fixed 16384-entry debug-line pool. Its draw
  is budgeted to stay inside that pool, and the multiplayer server frame now
  clears the lines as single player does (a remote client already cleared them
  on every snapshot), so a listen server's picture no longer goes stale once
  the pool fills.
- Enemy knockback can still throw a bot off a walkway, but a bot no longer
  fires a splash weapon whose predicted push would throw a team-mate onto
  ground that does not hold them, team damage on or off
  (`BotCombatSplashThrowsTeamMate`). The prediction follows `RadiusDamage` and
  `idPlayer::Damage`; a team-mate's own air control is not assumed. A
  penetrating hitscan (the multiplayer railgun) is held whenever a team-mate is
  anywhere on its line, behind the target included
  (`BotCombatPenetratingShotHitsTeamMate`).
- Swimming bots dive for a route corner under the water while they have more
  than half their air and surface otherwise. Routes are not planned around
  air, so a submerged passage longer than a breath is not modelled.
- A character's preferred team is only ever a hint. With `si_autoBalance` on,
  the server overrides the requested side whenever the teams are uneven, and a
  team switch kills and respawns the player.

## Testing

### Skill levels, 1 November 2026

Once the aim fix had given every level a real fight, the question was whether
the five levels are evenly spaced. `mp_bot_skill_ladder.py --skills 1,2,3,4,5`
put every level in each match on the plain curve: q4dm1, q4dm2, q4dm7 and
q4dm8 for ten minutes each per round, two or three bots per level. One
Bradley-Terry fit pooled all 22 matches (6,362 kills between levels). Each
level kept a separate rating for every revision of its row, and the spreads
come from resampling whole matches.

As committed with the aim fix, the steps were 99, 56, 57 and 60 rating points:
skills 2 to 5 were evenly spaced, and skill 2 stood far above skill 1. The
rating does not follow a row's interpolation linearly. Moving skill 2 a
quarter of the way toward skill 1 changed nothing measurable; nor did lifting
skill 1 a third of the way toward skill 2. Moving skill 3 a quarter of the way
toward skill 2 cost it 38 points, and moving skill 2 40 % of the way toward
skill 1 cost it 16. So each candidate was measured rather than predicted. One
round planned from a prediction was stopped and replanned, and a single round
of 1,153 kills put the skill 3 to 4 step at 17 points against a pooled 57.

Shipped: skill 2's row sits 60 % of the way from skill 1's to where it was;
skills 1, 3, 4 and 5 are unchanged.

| Step | As committed | Now |
| --- | --- | --- |
| 1 → 2 | 99 | 83 ± 11 |
| 2 → 3 | 56 | 72 ± 14 |
| 3 → 4 | 57 | 57 ± 9 |
| 4 → 5 | 60 | 60 ± 10 |

Every step is within 1.4 spreads of the 68-point mean, which is consistent
with even spacing, and the largest step is now 1.45 times the smallest (it was
1.77). Relative to skill 1 the levels rate 0, 83, 155, 212 and 272: skill 5
takes 59 % of the kills between it and skill 4, and 83 % of those between it
and skill 1. The confirming round on the shipped curve alone (1,857 kills, 15
bots a match) gave steps of 89, 69, 65 and 60, with:

| Skill | Kills : deaths | Accuracy | Median damage per bot-minute |
| --- | --- | --- | --- |
| 1 | 128 : 396 (0.32) | 21% | 227 |
| 2 | 213 : 373 (0.57) | 23% | 307 |
| 3 | 291 : 309 (0.94) | 39% | 376 |
| 4 | 425 : 287 (1.48) | 54% | 528 |
| 5 | 533 : 225 (2.37) | 68% | 659 |

These are bot-against-bot ratings. How far apart the levels feel against a
person is a separate qualification.

### Combat strength, 1 November 2026

The top skill level was easy, and the cause was structural rather than a
tuning number. The aim had no anticipation at all. The belief trailed a target
moving at a steady speed by speed times `aimTrackTimeConst` for as long as it
kept moving: 26 units at skill 5 for a 320 unit/s runner, outside a 32-unit
hull. On top of that, the slew was damped against standing still, so it trailed
anything crossing the view by `2 x turnDamping / natural frequency` of its
motion: 0.22 s at skill 5, two body widths on a strafer at 600 units. The
tighter a level's fire cone, the more of those misses it refused. Skill 5
therefore fired rarely and dealt about as much damage as skill 1, which
sprayed. The fix is the anticipation, pursuit and relative-slew gate in
[the aim model](#the-aim-model), plus long bursts for skills 4 and 5.

An offline model of `UpdateAim` and the trigger gates, run against a
320 unit/s target with multiplayer acceleration and friction, gives the share
of frames on which a hitscan weapon's view ray is inside the hull and the
trigger is open:

| Skill | Steady runner, 600 u | Strafer, 250 u | Strafer, 600 u | Juker, 600 u |
| --- | --- | --- | --- | --- |
| 1 | 2.5% → 7.9% | 7.4% → 6.8% | 7.8% → 7.3% | 17.3% → 16.1% |
| 3 | 0.7% → 33.3% | 3.3% → 12.4% | 8.0% → 16.7% | 19.2% → 13.6% |
| 4 | 0.0% → 56.9% | 1.3% → 30.6% | 4.9% → 35.6% | 15.1% → 16.1% |
| 5 | 0.0% → 86.3% | 1.0% → 49.4% | 2.6% → 56.4% | 7.7% → 31.5% |

The strafer reverses every 400-1200 ms and the juker every 250-600 ms. Before
the fix the lag met a reversing target only as it swung back through it, so
the worst bots hit jukers most often.

`mp_bot_skill_ladder.py` on q4dm1, three bots a side for 180 s, plain skill
curve, with only the MP module swapped in one runtime (`.install` as staged at
18:35, main `f62e521ea`):

| Match | Module | Cross kills high:low | Damage per bot-minute, high / low | Accuracy, high / low |
| --- | --- | --- | --- | --- |
| 5 vs 1 | before | 7:3 | 216 / 199 | 51% / 12% |
| 5 vs 1 | after | 17:5 | 446 / 224 | 68% / 19% |
| 5 vs 5 | before | - | 169 (both groups) | 42% |
| 5 vs 5 | after | - | 487 (both groups) | 63% |
| 5 vs 3 | after | 20:9 | 439 / 362 | 71% / 31% |
| 3 vs 1 | after | 17:5 | 368 / 250 | 32% / 17% |

Before, skill 5 fired 278 rounds in the 5 vs 1 window against skill 1's
1,037; after, 525 against 761. The skill 5 mirror went from 25 kills to 60.
Each level now beats the one below it. These are single short matches, so the
ratios are noisy; the damage and accuracy columns are the steadier figures.
The asset-free bot contracts and the native bot fixtures
(`mp_bot_regressions.py`, `mp_bot_jump_trajectories.py`) pass. Not measured:
play against people, which is the qualification that matters for how hard
skill 5 feels.

### Unpressed WALK falls and timing evidence, 28 October 2026

The five-minute six-bot DM8 audit in
`.tmp/mp-completion-20261028/bots-dm8/` retains one navigation death and one
stuck event in 284.928 simulated seconds, with 56 combat kills. Strauss fell
near `(601, 685, 314)` without a logged enemy impulse. The 2.192 ms weighted
mean and 12 ms worst-window p95 are diagnostic only: `bot_debug 3` adds
per-frame trace work and compilers/other runtimes overlapped this match.

`bot_probeWalk` installs an ordinary walking leg without the artificial
knockback state that `bot_probeMove` intentionally uses for impulse recovery.
It accepts the same origin/velocity/target arguments and an optional following
jump target, under the existing isolated local cheat-server restrictions.
The earlier and later retained grounded poses both survive walking toward
`(628,700,328.25)`, with or without the following jump. Steering diagonally
from the later pose toward `(652,660,328.25)` instead reproduces a world fall
in both cases. Those artificial legs isolate a defect; they do not establish
the original match's complete input sequence.

The grounded edge brake can lose support on a sloping rim before it stops.
Air recovery previously admitted only knock-caused falls, so the bot resumed
steering for its WALK corner through the void. It now admits a descending,
unpressed WALK fall whose coast is proven unsafe. Planned drop/transport
departures, voluntary jumps, ascent, liquids and safe arcs keep their prior
ownership. A jump following WALK cannot claim a takeoff that was never pressed.
Native production-function cases cover these boundaries, and reverting to
knock-only admission fails. All six actual-map probes survive with exit 0 and
unchanged engine/module hashes in `walk-recovery/`; the two diagonal cases die
with the prior module in `walk-diagonal/`. The reusable runner is
`tools/tests/mp_bot_walk_replay.py` (`--expect-unsafe` checks the old control).

The separate `bots-dm8-trace/` debug-4 match completed but its fourth causality
replay refused an out-of-date stage after another build advanced. Its
`diagnostic.json` retains all five world deaths as unqualified impulse
candidates. It is neither a passing safety run nor timing evidence. Replays
now retain their process exit, completion and runtime identity in `run.json`;
a launch/staleness failure returns a counted death and diagnostic instead of
discarding the enclosing audit report.

For controlled budgets, use `mp_bot_audit.py --debug 1` or `2`, an explicit
`--max-think-mean-ms`, `--max-think-p95-ms`, `--max-nav-build-ms` and
`--require-uncontended`. The runner rejects timing budgets with frame-by-frame
debug levels, records overlapping runtime/compiler processes, and rejects a
changed runtime. Percentiles are conservative ceilings per complete profiling
window, not an aggregate of percentile values. The full long-match/catalog
qualification remains under MP-02/MP-03.

Everything below runs in-tree and in CI with no game assets; the compiled
fixtures need a C++17 compiler (`clang++` or `g++`, or `CXX`).

- `tools/tests/mp_bot_navigation.py` pins the agreements that make navigation
  work and that the compiler cannot check: the engine handing back the
  allocated slot, bots re-begun after a map change, user info restored on every
  update, bots thinking before entities, and the navmesh deriving its agent
  from the movement cvars. It guards the body-free agent sweep, walk-door
  classification, transport sealing, the walk slope bound and its per-sample
  rise bound, the natural pad landing's supported node and centre proof, the
  rim-node flag and its link penalty, the line's ground-beside test, weighted route
  costs, the landmark heuristic and goal pricing, dynamic-only edge validation
  and repair, roaming retries, off-route recovery and its displaced-leg proof,
  the drop push-through, and the action entry/airborne/grounded-exit contract.
- `tools/tests/mp_bot_navmesh_search.py` splices the search core - components
  and their closure, landmark selection, the heuristic, both cost fields and
  pair pricing - into a native fixture and proves it on random two-floor graphs
  with one-way drops, cheap transports and sealed pits: the closure equals
  breadth-first reachability on 6,000 pairs, pair pricing equals the exact
  route cost, the heuristic never overestimates and is consistent on every
  edge, and ranked goal pricing returns exactly the exhaustive field's best four
  in 180 runs while expanding 6% of its nodes.
- `tools/tests/mp_bot_jump_trajectories.py` executes production hull construction,
  support, floor, step and jump proof against an independent convex sweep
  fixture using the engine's cylinder vertices. It rejects the
  narrow lower ledge and island cases that a vertical landing projection
  accepted, unsupported corner/rim nodes and landings, lethal floors, and
  raised lips whose running air step overshoots. Broad raised and lower floors,
  normal steps and gaps, a 24-unit platform, a floor seam and a 45-degree
  slope provide positive cases. Discretionary hop cases cover a platform where
  walking is safe but jumping overshoots, opposing initial momentum, and
  lethal/transport landings. The former proof fails the narrow 280-unit descent
  regression. Landing-impact cases cover descending jumps, drops and pads,
  with the same no-damage surface and water reductions as player physics.
  Running-drop cases reject small islands and preserve broad lower floors;
  landing continuation cases distinguish connected ground from a gap or rim,
  with low-ceiling, small-lip and boosted-hop regressions. The production
  `WalkableLine` runs on the same world: q4ctf1's chord onto the lip of a
  64-unit platform and a corner on a 48-unit lip are refused for their rise
  between samples and onto the end, while the same climb as a stair of
  player-height steps, level ground, a single step up and a step down walk;
  a line four units past a platform's edge is refused for hanging over it
  while one four units inside walks, and the rim-node test flags a hull
  hanging more than eight units over an edge but not one beside a wall or at
  the bottom of a stair.
- `tools/tests/mp_bot_regressions.py` compiles the production decision
  functions against a deterministic world: goal replacement and stall
  recovery, inventory changes, hearing and item timing, fight-or-flight,
  arming and duty pickups, perception against exhaustive target selection
  over 240 cases, one goal-pricing search for 80 pickups, grenade bounce
  threats, bot message queues that never accumulate, and pad holds that brake
  through reserved entry, retain real flights and reconsider blocked routes,
  plus adoption of observed unplanned pad launches without replacing goals.
  Two bots at opposing pads (including long pads that land beyond each
  other's launch) never launch together: the earlier published approach
  wins. The transport guard brakes momentum short of an unplanned pad,
  slides a wish away from it or stops, and never reverses a velocity back
  into it; chained pads must all be survivable; and a strongly knocked bot
  over nothing brakes its fall, including mid-flight, while pad launches and
  later traversals keep their controllers.
  Landing-health cases cover the live damage service and armour, immunity,
  alternate pickup routes, cached pre-entry checks and preserved flights.
  Production movement projection and player command scaling are checked
  together at 0, 45 and 90 degrees, including partial input and zero movement.
  Transition cases cover transverse jump/drop approaches, unsafe actual landing
  continuations, same-direction pad followers and pad completion only on
  grounding or an explicit chained handoff.
  Hop ownership cases change goals, view yaw and speed boosts during flight,
  then verify release on landing, failed launch, death, expiry and pad transfer.
  Recovery cases exercise both edge-guard branches, inland rescue from a
  marginal footprint, and actual command yaw with a nonzero view delta.
  Actual box/cylinder rim fixtures distinguish continuous inland rescue from
  unsupported tangency, a distant floor across a gap, walls and hazards.
- `tools/tests/mp_bot_team_orders.py` runs the team-order parser on phrasings
  that must and must not be orders and pins who may give them, when they
  lapse, and how every mode's planner reads them.
- `tools/tests/mp_bot_intelligence.py`, `mp_bot_goal_contracts.py`,
  `mp_bot_combat.py`, `mp_bot_objectives.py` and `mp_bot_team_balance.py` pin
  goal selection, weapon-aware spacing and pursuit, splash and trajectory
  safety, threat prediction, CTF/Freeze Tag/DeadZone roles with incumbency,
  and pre-spawn team reservations.
- `tools/tests/mp_bot_characters.py` covers the personality layer: the trait
  table and baseline curve against `botTraits_t`, monotonic skill curves, and
  every shipped `.style`, `.bot` and `.chat` file, including exactly eight
  unique lines per chat event per character and the tokens each event may use.

**Live audit.** `tools/tests/mp_bot_audit.py --basepath <retail-install>` runs
a hidden listen server with a fixed roster for a fixed time and reduces the log
to the numbers that describe a bot implementation: kills, deaths to the map,
navigation failures by kind (no route, stuck, off route, abandoned goals,
timed-out traversals), the `bot_profile` cost rows, how close the simulation
kept to real time, and process CPU. `--map`, `--gametype`, `--bots` and
`--skill` choose the match; `--team-order "everyone follow me"` has the
observer give an order and reports its team-mates' distance to it. Use the
optimized `builddir-perf` runtime for absolute costs; `builddir` is a debug
build and is several times slower.
The report records SHA-256 identities for the client, MP module and runtime
packs and refuses a stage older than `builddir`. `--debug 2` includes traversal entry positions
and velocities; `--max-world-deaths 0` turns any complete-run death to the map into
an audit failure. A default audit remains a measurement run, so its successful
exit alone does not establish safe navigation. A fatal engine-log error stops
the audit's own hidden client promptly, including a content-pack mismatch.
Timed rates retain their measurement window; warmup and complete-run world
deaths are separate report fields and both contribute to the safety gate.
The report also keeps the final team scores (CTF captures, DeadZone points)
and every bot's score.

**Skill ladder.** `tools/tests/mp_bot_skill_ladder.py --basepath <retail-install>`
measures what each skill level is worth in a fight. A hidden listen server
runs a free-for-all between groups of bots that differ only in skill, with
`bot_characters 0` so no style or character separates them. `--skills` names
one group per entry (default `5,1`; `1,2,3,4,5` puts the whole curve in one
match, so every level fights every other under the same conditions) and
`--per-group` sets its size (default two). Each `--map` is a separate match;
they run at the same time and pool. From the kill matrix between groups the
report fits a Bradley-Terry rating per group on the Elo scale - the chance a
kill between two groups goes to the first is `r1 / (r1 + r2)`, and 400 points
is ten to one - and prints the step between adjacent levels. It also reports
each group's accuracy, per-weapon shots, hits and kills, and median damage per
bot-minute, all differenced from `botlist weapons` at the start and end of the
window (the median because the statistics count damage before it is clamped
to the victim's health, so one telefrag adds about ten thousand).
`--require-ordered` fails unless every level outrates every level below it and
`--min-step <points>` unless every adjacent step reaches that size. Kills
cluster within a match - who spawned where, which fights a map makes - so with
three or more matches the report gives each step a spread from resampling
whole matches, and `--pool <report.json>` adds an earlier run of the same
`--skills` to the ratings. Counting noise alone understates it: two rounds of
about 1,200 kills each disagreed on one step by 40 points. The levelling
evidence below pooled 22 matches.

**Impulse-exclusion policy (decided 21 October 2026).** The traversal gate
counts navigation and control deaths. `--classify-impulses` (with
`--debug 2` and `--max-world-deaths`) applies it through
`tools/tests/mp_bot_impulse_replay.py`, which also runs on its own against
an audit report. A world death is excluded only when both hold:

1. a logged enemy `BOT_IMPULSE` precedes it - another client in DM or
   Tourney, a client on the other team (from the nearest `MP_PLAYER` report)
   in team modes - at most six seconds before the death and after no earlier
   death of that bot;
2. an isolated replay of the last such impulse's logged origin, velocity,
   remaining health and knockback time, one bot and no enemies on the same map, falls to the
   world under every one of sixteen fixed full air-input headings
   (`bot_probeAir`) and with no input. Pushes after it are reported but not
   replayed: an unrecoverable replay shows the enemy's impulse alone decided
   the fall, and a recoverable one leaves the death counted;
3. the same moment without the impulse - the logged velocity minus its kick,
   at the health before the hit - lands in at least one of three
   counterfactual trials (no input, braking, the bot's controller). A bot
   already falling into nothing when a machine gun tapped it was not killed
   by the tap.

Any trial that lands alive, any trial that fails to arm, an incomplete
session or a missing replay keeps the death a navigation failure; so do
deaths with no impulse, with only self/team/world impulses, with an
impulse outside the window, or with unknown teams. The bot's own controller
is replayed too (`bot_probeMove`, informative only). Excluded deaths stay in
the report with the impulse and every trial's outcome. The replay restores
the knockback time - no ground friction and only air acceleration, as
`idPhysics_Player` applies after a hit; without it every grounded rail slide
looked recoverable - but omits any later hit, so it can only find a fall
easier to recover than the match did, and sixteen headings sample - rather
than prove - the air-control envelope. `tools/tests/mp_bot_death_policy.py` runs the
rule without assets: one positive control and twenty-one negative controls,
each of which must stay a navigation death.

On `mp/q4dm5` with eight skill-4 bots in a debug build, the October 2026 pass
took the server from 0.32 to 0.96 of real time, stuck reports from 29 to 3-4
per two minutes, failed path searches from about 10% to under 2%, and goal
pricing from about 7,600 settled nodes per decision to about 2,200 expanded,
with no deaths to the map.

The 14 October 2026 review was measured as a same-day A/B on the optimised
build from a candidate runtime on loopback (`.tmp/mp-bots-review/`): HEAD's
unmodified module and the refined one each hosted eight skill-4 bots for two
minutes on `q4dm5`, `q4dm1` and `q4ctf1`.

| | q4dm5 before / after | q4dm1 before / after | q4ctf1 before / after |
| --- | --- | --- | --- |
| kills | 15 / 16 | 27 / 21 | 14 / 14 |
| deaths to the map | 0 / 2 | 0 / 0 | 0 / 0 |
| stuck reports | 6 / 6 | 0 / 3 | 8 / 6 |
| goals given up | 0 / 0 | 0 / 0 | 1 / 1 |
| traversal timeouts | 1 / 2 | 1 / 0 | 0 / 0 |
| think ms per frame | 0.87 / 1.76 | 0.74 / 0.88 | 0.93 / 1.40 |

Both q4dm5 deaths followed rocket knockback into the void, the documented
limit; the repeated q4ctf1 stop at -2596 884 186 (a walk corner 57 units above
the floor the bots reach it from) is in both runs and predates the review - it
is the string-pull sweep clearing a riser, fixed the same day (below). The
"after" think times overlapped a parallel module build on the same box. The
final module's rerun, with no build running but with the box otherwise about
half again as busy as during the baseline (`cpu_cores_busy` 0.79-0.96 against
0.57), gave:

| map (mode) | kills | deaths to the map | stuck | no route | given up | timeouts | think ms | nav ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| q4dm5 (DM) | 14 | 1 | 5 | 1 | 0 | 0 | 1.42 | 0.61 |
| q4dm1 (DM) | 30 | 0 | 1 | 0 | 0 | 0 | 1.33 | 0.36 |
| q4dz1 (DeadZone) | 22 | 0 | 3 | 0 | 0 | 2 | 1.02 | 0.21 |
| q4dm3 (Freeze Tag) | 23 | 0 | 0 | 2 | 0 | 0 | 0.88 | 0.21 |
| q4ctf5 (CTF) | 18 | 6 | 3 | 0 | 1 | 0 | 0.70 | 0.12 |

The navmesh is identical before and after on every map (same nodes, links,
gap jumps and sealed transports), and no route needed a repair pass in either
build. The two DM maps expanded more nodes per search after the review (165
against 98 on q4dm5, 76 against 46 on q4dm1) because the bots chose different,
longer-run goals, not because searching got dearer; with the busier box that is
the whole of the think-time difference. Pickup refusals on q4dm5 fell from six
to one, which is the pad-brush goal test at work. The baseline module was then
run on the two pad-heavy maps as well: q4dz1 went from 7 stuck reports and one
goal given up to 3 and none, and q4ctf5 from 3 deaths to the map (plus one
suicide) to 6, with 2 stuck reports against 3. q4ctf5 keeps its known
character: five of the six deaths were bots over the void after a knockback or
an unplanned flight, and one was the observer. The bots now carry the rocket
launcher rather than the rail gun between fights there, so more of the hits
that start a fall are rockets, which is also how players die on that map.
q4dz1's two timeouts were one bot bouncing on a vertical pad whose ledge needs
air control, the q4dm1 pad case on another map. Neither pattern is new to this
build; both were fixed in the navmesh the same day (below).

The same-day follow-up fixed both in the navmesh (the per-sample rise and
side-of-corner proofs for string-pulled legs, and the supported, centre-proven
natural pad landing) and was measured the same way, eight skill-4 bots for two
minutes per map from an optimised candidate runtime on loopback, twice on the
two maps concerned and once on the two pad-heavy DM maps as a regression
check:

| map (mode) | kills | deaths to the map | stuck | no route | given up | timeouts | think ms | nav ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| q4ctf1 (CTF), run 1 | 17 | 0 | 3 | 0 | 0 | 0 | 0.95 | 0.44 |
| q4ctf1 (CTF), run 2 | 17 | 0 | 7 | 0 | 1 | 0 | 0.89 | 0.37 |
| q4dz1 (DeadZone), run 1 | 24 | 0 | 8 | 0 | 0 | 0 | 0.64 | 0.16 |
| q4dz1 (DeadZone), run 2 | 25 | 0 | 6 | 0 | 0 | 0 | 0.68 | 0.18 |
| q4dm5 (DM) | 21 | 0 | 8 | 0 | 1 | 0 | 0.75 | 0.30 |
| q4dm1 (DM) | 33 | 0 | 0 | 0 | 0 | 0 | 0.68 | 0.22 |

No bot pressed against the wall under the q4ctf1 ramp corner in either run,
and no pad traversal timed out on q4dz1 in either; both of q4dz1's vertical
pads were completed onto their ledge. Each stuck report now names the leg the
bot could not finish and the navmesh's current verdict on it, which is how
the remaining reports were read: on q4ctf1 they are bots walked off the
platform's rim or the ramp's side by combat footwork mid-duel and then far
from their corner, plus the rim walk along the 64-unit platform's edge nodes
that hang four units past its riser (the pre-review runs show the same
"off route ... below corner" pattern); on q4dz1 they moved to the catwalk rim
at (±196, ∓2250..2396, 384), whose edge nodes hang over a 190-unit drop and
which the pre-review baseline also stopped at three times in its own run. The
navmeshes are identical to the review's in areas, components and dead ends on
all four maps; the only link changes are the pad landing nodes - q4dz1's two
vertical pads land a hull further onto the ledge, q4dm5's pad 15 and 29 and
q4dm1's pad 18 land on the supported node beside their old one, and q4ctf1's
pad 17 is unchanged. An earlier candidate that held the natural landing to
every side of the pad sealed twenty of q4dm5's pads and split q4ctf1 into
nine areas; that is why the natural landing is proven from the centre only.

The two patterns those runs left - the rim walks and the combat-displaced
stops - were fixed next (rim nodes priced, the line's ground-beside test and
the displaced-leg proof above) and measured the same way, twice, the second
time back to back with the previous module under the same box load (the
"cores busy" column is the whole box during the audit window; the second
pair ran while two other builds were compiling):

| map (mode) | module | kills | stuck | given up | timeouts | think ms | nav ms | cores busy |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| q4ctf1 (CTF) | previous, back-to-back | 29 | 3 | 0 | 0 | 1.44 | 0.54 | 0.68 |
| q4ctf1 (CTF) | this, run 1 | 19 | 0 | 0 | 0 | 1.81 | 0.81 | 0.77 |
| q4ctf1 (CTF) | this, run 2 (back-to-back) | 16 | 0 | 1 | 0 | 2.21 | 0.93 | 0.94 |
| q4dz1 (DeadZone) | previous, back-to-back | 26 | 9 | 2 | 0 | 1.16 | 0.26 | 0.69 |
| q4dz1 (DeadZone) | this, run 1 | 27 | 0 | 0 | 0 | 2.06 | 0.67 | 0.60 |
| q4dz1 (DeadZone) | this, run 2 (back-to-back) | 30 | 0 | 0 | 0 | 1.20 | 0.33 | 0.86 |

No bot reported stuck in the measured window of either run on either map;
the previous module's runs the same morning had 7 and 3 on q4ctf1 and 6 and
9 on q4dz1. The one report left anywhere is in q4dz1's warmup, at the toe of
the catwalk's ramp (toward 172 -2156 334), which the pre-review baseline hit
at the mirrored end and which is neither a rim leg nor a displacement. The
nodes, areas, components and dead ends are identical to the previous module
on both maps; q4ctf1 has one gap jump and three links fewer, a landing
continuation the ground-beside test now refuses. Off-route reports rose to
40-50 per run, which is the displaced-leg proof at work (the previous module
reported 5-10), while path searches per profile row did not (268 to 279 on
q4ctf1, 187 to 196 on q4dz1): a displaced bot's reroute replaces a search it
would have made after the stuck detector anyway. The navigation cost per
frame is higher by about the share the busier box explains - the cost-field
share, which the change does not touch, rose by the same proportion - plus
the two support-box sweeps the ground-beside test adds to every interior
sample of a shortcut proof. Kill counts vary from 14 to 29 per run on q4ctf1
across the morning's runs and say nothing either way.

That single-map result does not qualify every stock map. The subsequent
32-map sweep found world deaths on q4dm8, q4ctf3, q4ctf5 and q4dz3. Follow-up
six-bot, 45-second audits exposed hull/support mismatches, unsafe upward hops,
pad steering ownership, low-health landing risk and unsafe landing handoffs.
The landing candidate recorded one warmup and four measured world deaths;
the warmup fall revealed why qualification must include the complete run.

The subsequent control candidate completed all four affected maps for 45
seconds each, with one measured world death per map and no warmup deaths or
traversal timeouts. All engine processes exited normally; the strict safety
audit failed. The DM8 fall followed a documented enemy impulse. CTF3 exposed
a routed jump launched with large transverse momentum outside its graph
proof. CTF5's fall followed unclassified non-enemy damage; the log cannot
separate self/team impulse from steering. DZ3's low-health hard-fall death
strongly matches an accidental launch from an active pad omitted from the
transport registry. The exact-state jump gate and unroutable-pad sealing
corrections pass all 12 suites, including production registry/sealing fixtures;
their staged rerun and the final 32-map sweep remain pending. No cross-map
safety or optimized-performance qualification is claimed. These four debug
maps built navigation in 986–3,267 ms and ran at 0.963 of real time; these are
diagnostic observations, not controlled performance comparisons.

### Current four-map rerun, 18 October 2026

The staged code recorded by `6de4e622e` reruns the exact-state jump and unroutable-
pad corrections with six skill-four bots, 45 measured seconds, hidden/windowed
OpenGL and original stock assets. The unchanged audit gate permits zero world
deaths across warmup and the measured interval. All four processes exited cleanly.

| Map and mode | Warmup world deaths | Measured world deaths | Safety gate |
| --- | --- | --- | --- |
| q4dm8, DM | 0 | 2 | Failed |
| q4ctf3, CTF | 0 | 0 | Passed within this run |
| q4ctf5, CTF | 0 | 1 | Failed |
| q4dz3, DeadZone | 0 | 0 | Passed within this run |

DM8's first fall follows two recorded enemy hits, including transverse velocity
of -553 and then a further impulse near the outer ledge. Its second fall follows
self damage and an enemy hit during a dodge flight. CTF5's fall follows an enemy
hit while crossing a pad route: the logged forward speed changes from the pad's
-646 launch to -154, followed by a fall below the landing. These are observed
combat-impulse sequences, not proof that subsequent steering is optimal or that
every fall was unavoidable. They remain failed safety evidence; no threshold,
physics rule or navigation behavior was relaxed for this rerun.

| Report | SHA-256 |
| --- | --- |
| `.tmp/s2bots/q4dm8/report.json` | `8cc40d179a6ac9899f2493e3f44a2080f825613519e7a69b86aeaa480946d126` |
| `.tmp/s2bots/q4ctf3/report.json` | `cfc1bc6062c5e1a01e88c4b53c2569070518a10a53d5b0ae31f4d07536efc9d5` |
| `.tmp/s2bots/q4ctf5/report.json` | `15ffbe1feda78a72b40fd94953322093f054b17e5a5de3d348b65ff313c4501e` |
| `.tmp/s2bots/q4dz3/report.json` | `7bbc7f7e7748d14765c4b8c8e56b6b3bbfd92c5499eacb2e13757ccda3aa7187` |

Each report retains the exact runtime hashes and names its engine log. The current
stock `pak019.pk4` map declarations list 33 multiplayer maps when `q4dm11v1` is
included; the following sweep covers every declared variant.

### Current complete catalog sweep, 18 October 2026

All 33 maps completed under the same six-bot, 45-second, hidden/windowed GL
profile and zero-world-death gate. Thirty-two passed; DM8 failed with one measured
world death and no warmup deaths. CTF3, CTF5 and DZ3 passed in this sweep. That
does not erase CTF5's preceding failure or establish repeatable safety.

DM8's Rhodes took a logged enemy hit from Strauss at game time 19,136 ms near
`703 555 324`, leaving velocity `328 290 127`; the world death followed at
21,264 ms near `683 1040 -1154`. No voluntary dodge flight is logged around
this fall. The combat impulse is established, but the adequacy of edge avoidance
and recovery steering remains unproven. MP-02 stays open with the strict gate
unchanged. Across the catalog the diagnostic logs also contain 37 stuck events,
10 goal give-ups and one traversal timeout, requiring route-specific review.

The source is `6de4e622e`; each map's report under `.tmp/s2bots-full/` retains
runtime hashes, map/navigation statistics and its engine log. The manifest binds
the 33-map selection to the retail catalog archive. The completed records are:

| Record | SHA-256 |
| --- | --- |
| `.tmp/s2bots-full/manifest.json` | `c10d0707664f9c50beb102d332c72f7c7c86918f9c112cfaa6789336a8f699e7` |
| `.tmp/s2bots-full/summary.json` | `1b1b6457bb513df0fa6db2c1414a6fb0b56db383e490ecea95d81d46691b3fdf` |

Concurrent build/probe work was present during portions of this functional sweep.
Its diagnostic timing is not a controlled performance qualification or evidence
that the route-generation/think budget is accepted.

### Sloped-floor support correction, 21 October 2026

CTF6 edge-braking traces exposed a false rejection on ordinary sloped ground.
The eight-unit support box rests below the full player's origin on a slope;
testing the full hull at that lower height starts inside the floor. The landing
check now derives the height difference from the contacted plane and the two
actual hulls, then performs a full-hull floor sweep and the existing surrounding
support/hazard checks. It does not raise the hull a whole step, which would reject
low ceilings. Support sweeps also reach just below their one-step limit so a
surface exactly at that limit is inside the sweep rather than its no-hit endpoint.

The production-function fixture covers four player hull settings across seven
slope headings/gradients, including three units of headroom, an insufficient
ceiling and lethal ground. Existing unsupported-rim, gap, seam, jump/drop and
transport cases pass on Windows and Linux/WSL. Independently restoring either
old function makes its regression case fail. Native Windows compilation and
staging pass. This is a geometric/controller correction, not a player-physics
change or a performance qualification.

On original CTF6 geometry, agent/support floor heights differ by 1.48 units near
`208.95 2251.32 -117.69`, 1.32 near `411.06 -392.02 -93.05`, and 2.23 near
`300 2284 -153`. All six current continuation checks pass. The engine-rendered
capture shows the sampled slope in the loaded CTF map. The first probe omitted
explicit navmesh generation and failed; its report remains beside the corrected
run. A subsequent six-bot, 60-second CTF6 run exits normally with zero complete-run
world deaths and zero traversal timeouts, but four stuck events. The earlier
diagnostic run's repeated Rhodes/Voss obstruction and the remaining route stalls
are not closed by this slope fix.

Evidence is under `.tmp/stage-two/ctf6-slope-geometry-r2/`,
`.tmp/stage-two/ctf6-edge-trace-r2/`, and
`.tmp/stage-two/bot-slope-negative-controls/`; reports bind the tested source and
runtime hashes. The earlier `ctf6-edge-trace-r1` support lines contain an invalid
normal field caused by temporary formatting-buffer reuse; their separate
assessment records that limitation. Current diagnostics print normal components
directly. The renewed four-map, six-bot, 45-second safety sweep passes CTF3 and
DZ3, but fails DM8 and CTF5 with one measured world death each and no warmup
deaths. DM8's Rhodes starts an upward routed jump near `-556 876 608` toward
`-572 844 632` and falls at 8,176 ms, with no recent recorded damage. That
sequence requires exact takeoff/flight diagnostics. CTF5's Strauss receives an
enemy hit at 39,184 ms, leaving velocity `434 615 -100`, and subsequently falls;
the log does not establish whether better recovery could prevent it. These
remain failures under the unchanged zero-world-death gate.

The renewed sweep lives in `.tmp/stage-two/bot-slope-four-map-r1/`. The combined
record `.tmp/stage-two/bot-slope-evidence.json` has SHA-256
`ec24904ec4aa6c5670e96b75c78da74b2f64f24fe95e5514bf0e7c1e3d02a850`.

### Live jump origin correction, 21 October 2026

The DM8 routed-jump fall was reproduced with one isolated bot, no enemy and
the recorded approach near `-556 876 608.25`. The predictor raised the live
collision origin by the one-unit navigation floor-probe offset. At a raised
ledge, that extra unit put its downward trace beyond the air-step distance.
It consequently predicted a wall slide that removed horizontal speed. The
actual player could step up and retained that speed, passing the landing before
descending. At the first divergent frame, the proof predicted horizontal
velocity `5.457 -5.457` while the player retained `-72.594 -83.483`.

Live jump proofs now start at the exact player collision origin. Graph probes
retain their existing clearance. The isolated replay refuses the unsafe
approach, repositions and completes the jump without a world death. An independent
box-world regression rejects the same air-step boundary on a narrow platform
while preserving a broad landing. Windows and Linux/WSL production-function
fixtures pass; restoring only the old live-origin offset fails the new case.
Player physics and jump strength are unchanged.

`tools/tests/mp_bot_jump_replay.py --basepath <retail-install> --output-dir <new-directory>`
repeats this check on original DM8 assets with a hidden window and isolated
writable profile. It requires a completed jump, zero world deaths including
setup, unchanged runtime/source identities and the rejection controls. The
new `bot_probeJump client x y z vx vy vz targetX targetY targetZ` diagnostic is
restricted to an isolated local cheat server with one bot and a separate local
observer; human clients, invalid client numbers and observer overlap are refused.
It drives the normal bot controller and player physics after setting the state.

Evidence is bound by `.tmp/stage-two/bot-jump-origin-evidence.json`, SHA-256
`30eadd5b410bfb1fec42bae4112f25792193f84ff1aab7b7ea0dd1f48d6766be`.
The failing original replays and the first permanent harness's echo-whitespace
parsing failure remain preserved. The corrected final replay and eight log
assessment controls pass. This closes the isolated prediction defect only;
the broader DM8 rerun still records a world death, and MP-02 remains open.

### Jump-pad impulse recovery, 21 October 2026

The four-map rerun after the live-origin fix passes CTF3, CTF5 and DZ3 with zero
world deaths. Its remaining DM8 death is a different flight: Rhodes launches
from `1039 106 14`, takes a 30-damage hit from Sledge at 39,920 ms, then falls
past the destination platform. The recorded post-hit state is `1538 108 434`
with velocity `822 -10 412`, aimed at `2452 76 256`. The old coasting check
accepted any projected touchdown beyond the destination and within its lateral
tolerance, without establishing that floor existed there.

Natural pad coasting now requires supported touchdown, a walkable connection to
the destination, and supported room ahead for the existing momentum lookahead.
When a route ends, edge braking runs before item/idle waits rather than waiting
for off-mesh recovery. Normal air steering corrects an unsafe projected landing;
player acceleration, gravity and pad strength are unchanged.

An isolated replay of the rounded post-hit state first reproduced the death.
Checking touchdown alone landed at `2601 89 256`, but momentum still carried the
bot off the edge; that failed intermediate run is preserved. The final replay
lands at `2492 80 256`, survives the remainder of the five-second sample and
exits normally with zero world deaths. This recreates the recorded position and
velocity through real player physics, not the complete original combat history.
Windows/Linux synthetic collision tests preserve safe natural and deliberately
steered landings. Independently restoring unchecked touchdown or removing the
runout check fails the corresponding regression.

Use `tools/tests/mp_bot_jump_replay.py --pad-impulse --basepath <retail-install>
--output-dir <new-directory>` for the pad case. Its `bot_probePad` diagnostic
uses the same restricted server/client checks as `bot_probeJump` and appends a
pad entity name after the nine coordinates. The pad must have a proven natural
landing. Nine log-assessment controls cover missing completion, a wrong traversal,
world death, rejected-client controls, fatal errors and abnormal exit. The
engine capture shows the original elevated platform. The current build also
passes the earlier jump replay; one intervening abrupt process termination
remains a failed run with an unestablished cause.

`.tmp/stage-two/bot-pad-recovery-evidence.json` binds the reports, source/runtime
identities, native controls and capture with SHA-256
`a3c4b436ab12241e2e6d0f40acb37a0e9c177b3d544f0fd81bf81261dc3c0777`.
This closes the two isolated pad-recovery defects. Other impulse recoveries,
the renewed map/catalog safety sweep, route stalls and controlled performance
remain under MP-02.

The next six-bot, 45-second four-map run is preserved in
`.tmp/stage-two/bot-pad-runway-four-map-r1/`. CTF3 and CTF5 pass with zero world
deaths. DM8 has one world death after a different enemy impulse during an
ordinary walking leg: Rhodes receives 74 damage at 19,008 ms at `-95 947 328`,
leaving velocity `-382 -5 259`, and subsequently falls. Its recoverability is
under investigation; it remains a failure under the unchanged gate. DZ3 stops
early with exit `0xFFFFFFFF`, without a recorded fatal error or normal shutdown.
No matching Application/System warning or error was found in the two-minute
event-log window. The cause is unknown. A separate repeat on the same runtime,
`.tmp/stage-two/dz3-runway-repeat-r1/`, completes with zero world deaths and
16 completed traversals. That repeat does not replace the interrupted record.

### Walking impulse controls, 21 October 2026

An isolated replay of the rounded DM8 walking impulse (`-95 947 328.25`,
velocity `-382 -5 259`) reproduces the fall. A control that commands maximum
eastward braking immediately also falls; the same control with horizontal
velocity reduced to `-200` survives. An engine capture shows the bot beyond
the platform edge. These tests preserve the actual player acceleration and
collision code, but do not recreate the original combat or knockback timer.
The free-flight acceleration bound does not establish that every alternate
landing is impossible. The original death remains a failure, not an accepted
exception to MP-02.

The three reports and their exact runtime identities are bound in
`.tmp/stage-two/bot-walk-impulse-evidence.json`, SHA-256
`3685bb773e0d690c07355f766ad78e66ae88d1015f3d97a3fc62df7b662ad388`.

### Renewed catalog sweep, 21 October 2026

The 33 declared retail map variants were rerun after the slope, live-origin and
pad-runout repairs: six skill-four bots, five seconds of warmup and 45 seconds
of observation, hidden/windowed Windows OpenGL. The unchanged zero-world-death
gate passes 31 variants and fails CTF5 and q4xdm13. Across the completed samples
there are 505 completed traversals, no traversal timeouts, 13 stuck events,
five goal give-ups, three no-route events and 26 refused pickups. These are
diagnostic runs with concurrent workloads, not controlled performance evidence.

CTF5 records a Voss world death without a preceding positive-damage callback,
then ends abnormally during another worktree's install. A later 60-second repeat
completes without deaths, but does not explain or replace the original failure.
The player damage path now logs every applied bot knockback at `bot_debug 2`,
including impulses with zero health damage, to distinguish those causes.
The first run with that added diagnostic was also interrupted during another
checkout's install; `.tmp/stage-two/ctf5-impulse-diagnostic-r1/` is incomplete.

q4xdm13 records a fast post-drop dodge that touches supported ground near the
west rim and then falls. The isolated reproduction and repair are below.
Catalog evidence is `.tmp/stage-two/bot-current-catalog-r1/`: manifest SHA-256
`c91f063e7f3e0ff484220e7db9c7e91332df092ceaf0690236511dc1ee698023`,
summary SHA-256
`1b3b35fc46269ecde34fcfb57cf1a58ac4ee033ce4734cec3fdefdac3b36c31f`.
The reports retain exact source/runtime identities and both failures. MP-02
remains open for the remaining cause, route stalls, objectives and cost gates.

### Dodge landing runout, 21 October 2026

Replaying q4xdm13's rounded post-drop state at `947 -390 -319.75`, velocity
`-575 81 0`, reproduces a world death without enemies. The old dodge proof
accepts touchdown near `547 -334 -320`; the real player lands near
`542 -333 -320` but carries enough horizontal momentum to leave the floor.
Supported touchdown alone was insufficient.

`DodgeJumpClear` now requires supported continuation along 0.15 seconds of
touchdown velocity, matching the grounded edge guard's lookahead. It tests
both actual momentum and the ordinary full-speed approach. Native Windows and
Linux fixtures reject the narrow landing and retain wider ordinary/sprint/haste
controls. Removing only that new decision fails the fixture. The original-map
replay rejects unsafe momentum, then permits a slower hop, completes grounded
and survives five seconds with no world death. Viewed engine captures show the
original platform and the changed landing position.

The permanent replay is `tools/tests/mp_bot_jump_replay.py --dodge-runout`,
with the usual asset/output arguments. `bot_probeDodge` uses the same isolated
one-bot/local-cheat restrictions as the other traversal probes and drives the
normal dodge admission/controller. Human and invalid-client controls pass;
removing the completion marker, rejection, denied momentum or grounded hop
causes the result checker to fail. No player physics or combat rule changes.

Evidence is `.tmp/stage-two/bot-dodge-runout-evidence.json`, SHA-256
`4d115459a3ce5a91ec5049ec0a69f1eeb391348c48cdca883b74e173dfcc84da`.
The native negative control's initial report used the wrong assertion-message
matcher; its retained log and independent assessment identify the actual
failing assertion. This bounded regression does not close MP-02's match,
catalog or performance qualification.

### Early pad reservation, 21 October 2026

The CTF5 trace catches Cortez and Strauss meeting head-on at game time 46864:
both opposing horizontal velocities collapse in that frame, followed by void
deaths. Cortez's reservation starts at 45968, only one command before the pad
launches him. The approach still has two walking corners inside braking range;
the old check considers only the corner immediately before the jump pad.

`HoldForPad` now looks through consecutive walking corners to the first action,
using the final walking corner and pad landing for the same finite corridor
check. An intervening jump, drop or other traversal stops that lookahead. The
actual trigger distance, momentum braking, timeout/replan and committed-flight
ownership remain in place. Windows/Linux production-function fixtures pass;
restoring the immediate-next-corner decision fails the regression. Normal bot
navigation checks and the staged Windows build pass.

The first runtime repeat completes without a world death but advances only
about 46 seconds of game time under detailed logging; it does not qualify the
map. The audit now reports observed simulation duration and accepts an explicit
`--min-sim-seconds` gate. Sufficient, short and missing clock samples are checked.
The next run is interrupted with `0xFFFFFFFF` and remains failed evidence.
A lower-logging repeat completes 110.816 measured simulation seconds, 78
traversals and no stalls/timeouts, but still has two world deaths: a 10-health
bot takes 10 landing damage, and another falls after an enemy rocket impulse.
These retain the strict map failure and need separate diagnosis. This run is
not a controlled performance result or complete catalog qualification.

Evidence: `.tmp/stage-two/bot-pad-hold-evidence.json`, SHA-256
`5fa183eb4f5681910142bd7f01441ef9d6fbe8a5c0681179ff93a45d2b28c5b1`.
It binds the original collision trace, native checks, rejected regression,
runtime identities and all three repeats. MP-02 remains open.

### Repeated pad contact and landing health, 21 October 2026

The isolated `func_jumppad_12` replay explains the earlier 10-damage landing.
Its sloped force brush sets the player's velocity on successive contact frames,
while the predictor previously applied one push. `PadLanding` now repeats that
velocity while the full player hull actually intersects the brush. A bounds
test only rejects distant positions; the collision model decides contact.
At the recorded launch `-647 445 14`, the corrected probe predicts impact
61.658, above the hard-fall threshold of 60. The original-map replay takes
10 damage and completes the pad, matching that prediction. Player physics and
damage rules are unchanged.

The retained-route health check also looks through consecutive walking corners
to the next traversal, so newly lethal landings trigger braking before the last
approach corner. Intervening actions stop the lookahead and committed flights
keep their landing controller. The earlier pad reservation regression now lives
in the existing native bot fixture; its initial standalone fixture is retained
only with the diagnostic evidence.

Windows/Linux production-function checks pass. Four negative controls reject
single-push prediction, bounds-only contact, late reservation and late health
checks. One initial native link failed because Windows still held the first
executable; separate fixture output paths let the retry reach its intended
assertion. The failed attempt remains recorded.

The next match completes 110.608 measured simulation seconds and 100 traversals,
with no traversal timeout, but retains one stall and four world deaths. Two bots
adopt opposing pads after unintended entries, a nine-health bot crosses an
unplanned chain of pads and takes 10 landing damage, and another receives a
railgun impulse during a committed flight. These are still failures under the
strict map gate. A separate exact post-rocket replay also falls without further
enemy damage. Further collision/impulse recovery, catalog and controlled timing
qualification remain open.

Evidence: `.tmp/stage-two/bot-pad-contact-evidence.json`, SHA-256
`f764f25ef8bdfa5b2e9cd77b4ac8632615724a86228f6dddd42b0105ff136fe0`.
It binds source/runtime identities, native controls and original-map logs.
The diagnostic screenshot commands in those runs failed to produce captures;
no visual acceptance is claimed.

A later walking-air-steering candidate still dies after the full recorded
rocket impulse. Both the candidate and existing steering survive a separate
half-horizontal-velocity control, so no safety gain is established and the
candidate is withdrawn. An initial control overlapped unfinished staging and
correctly failed its runtime-identity check; the completed-stage retry is the
valid comparison. Quoting screenshot filenames repairs the diagnostic commands;
the earlier zero dimensions were an argument-parsing mistake, not a renderer
failure. Evidence: `.tmp/stage-two/ctf5-air-recovery-investigation.json`, SHA-256
`7aa0a8d494e1dcffdd2907be76454d28d52b9b8599bfa25f5fa74ad5dc152255`.

### Installation interference, 21 October 2026

The three abrupt `0xFFFFFFFF` exits in the DM8 jump replay, DZ3 four-map run
and renewed catalog's CTF5 run coincide with installation intervals in other
worktrees. Each corresponding install log records stopping openQ4 by name.
The Windows wrapper selected every matching process on the machine and repeated
that global selection before forcing shutdown. This is strong evidence of
installation interference; no PID-level termination event was captured.

Installation now selects only executables directly in that checkout's `.install`
directory, retains their process handles across the close interval and leaves
unreadable paths alone. Six production-function cases cover foreign and nested
paths, a new process during shutdown, graceful exit, close refusal, concurrent
exit and failure to stop a live target. The original wrapper and independent
name-selection/reselection regressions all fail the test. The Windows staging
test runs this check in normal validation. Other checkouts need this fix before
their installations gain the same isolation.

Evidence is in `.tmp/stage-two/install-process-isolation-r2/report.json`, SHA-256
`1a38e2e2f9dd8a8a9f77fa0545359118bdde34afb1489e4adde2c413c2da6e8f`.
The interrupted runs remain incomplete evidence. In particular, CTF5 already
recorded a world death before shutdown; installation interference does not
explain or excuse that death.

### Pad corridors, unplanned pads and knocked falls, 22 October 2026

Full-duration q4ctf5 matches on the 21 October build still lost two to four
bots per two minutes. Fresh `bot_debug 2` traces of three baseline matches
(Vulkan hidden window, since hidden OpenGL clients stall on present while
the laptop's display is off) separate the causes:

- **Opposing pad races.** Two bots held at opposite pads (func_jumppad_3 and
  func_jumppad_4, either side of the floating armour) both saw the corridor
  clear on the frame the last flyer landed and launched within 16 ms; they
  met over the void. The same race later killed two bots on the long pads
  func_jumppad_5/func_jumppad_6, which land beyond each other's launch.
- **Unplanned pad entries.** Momentum carried walking bots onto pads their
  route did not choose. func_jumppad_14 lands its flyer on func_jumppad_15,
  which throws it across the map onto a 10-damage landing: a nine-health bot
  died on that chain. An isolated replay of momentum toward func_jumppad_14
  beside walking legs that avoid it entered the pad in 5 of 7 trials.
- **Low-health pad chains.** The retained-route health check examined only
  the first traversal, so a planned chain whose second pad was lethal was
  entered.
- **Knocked falls.** Rockets and rails threw walking, dropping and pad-flying
  bots off the walkways while their controllers kept steering for a corner,
  a drop's landing or a pad's landing. Isolated replays show several of
  these land under some fixed air input.

The fixes:

- Every bot within 320 units of the pad its route takes next publishes that
  approach; a bot gives way to an earlier approach (lower client number on a
  tie) whose flight shares its corridor - both of the other's ends within the
  corridor's width of its line and overlapping spans - while braking still
  starts only within hold reach. Exactly one of two opposing bots launches.
- Grounded movement looks ahead along its velocity, as the edge guard does,
  for any live pad or teleporter other than the route's planned one
  (`rvNavMesh::TransportOnMove`, exact brush test). Momentum that reaches one
  brakes; a wish that alone reaches one slides square to itself, away from
  the volume's centre, or stops - never reversing a velocity that already
  points away, which in the first form crept a bot onto the pad a unit at a
  time. Planned pads and committed flights are untouched.
- `GuardLandingHealth` requires every pad of a chain to be survivable.
- A strong knock (at least 150 units) now reaches the bot. When the fall it
  caused - thrown off the ground, slid off a ledge within 400 ms during the
  knockback's friction-free time, or hit in the air - would land on nothing
  with no input (`rvNavMesh::AirInputLandingSafe`: player air acceleration,
  gravity and the swept hull, landing supported, affordable and with room to
  stop), the bot holds the first input that lands among braking, the eight
  headings nearest to it, or none, re-chosen every 250 ms, through the fall
  and a short touchdown runout. A knocked jump, drop or pad flight hands over
  its route. Jumps the bot pressed after an earlier knock, a pad's volume and
  a pad flight that caught the throw keep their own controllers, and a strong
  knock ends a dodge hop's fixed input. Two intermediate forms of this guard
  failed in full matches and are recorded below; neither is in the result.

Each fix has a native regression in `MpBotRegressionsFixture.cpp` (Windows
clang and WSL Linux), and sixteen single-reversal controls - removing the
reservation, publishing only within hold reach, the old span test, no guard,
a guard that blocks the planned pad, reversing any velocity, sliding either
way, a single-pad health check, no air recovery, an unlatched brake, any
recent knock, a knock-slide window of one frame, transport flights not
owning the bot, a knocked drop keeping its route, always braking, and a
knocked dodge keeping its input - each fail their case on both hosts.
`tools/tests/mp_bot_jump_replay.py --unplanned-pad` repeats the
func_jumppad_14 replay: before the guard `westC` rode the pad chain; with it
no trial enters a pad.

**Getting there took several rounds, each found by a full match.** Three
baseline matches on the starting build recorded 8 world deaths (3, 2 and
3), including both pad races. With the reservation and the guard the next
three recorded 2, 0 and 1, all after enemy rails or rockets. The first
airborne brake then cost eleven deaths in one match - it braked planned pad
launches on the frame before their traversal was marked started, and its
early return kept the traversal from ever being marked - and was gated on a
knockback; the next match still braked a pad launch taken 1.3 s after a
machine gun's 25-unit nudge, so only knocks of at least 150 units count and
pad volumes own the bot. Later matches showed a drop the knock itself had
started, a far walkway reachable only along the throw, a grounded rail
slide during the knockback's friction-free time, a knock on the frame a
dodge hop started, four 49-unit shotgun pellets in one frame, and a
survivor landing onto a pad; each became the rule described above. The
policy tooling also changed on evidence: replays now restore the victim's
health and knockback time (without them every grounded rail slide and
low-health fall looked survivable), give each trial a freshly added bot
(a bot that walked off after its trial left the next one unarmed), and
replay the last enemy impulse even when a team-mate's nudge followed it.

**Qualification runs.** All use the staged Windows build, original assets,
six skill-4 bots, the Vulkan hidden window and `--classify-impulses`. Other
work on the machine kept it at 100% CPU for most of the evening (four
VibeStudio builds, about 25 compilers), so the four-map set ran 180 s of
wall time for at least 90 s of simulation and the catalog 90 s for at least
40 s; the frame and think columns below are contended numbers, not budgets.

The complete declared catalog (`.tmp/mp02/catalog-r5/`, build `409cee58`,
33 variants, warmup included): 29 variants pass outright. q4dm8 records two
world deaths - one excluded (a direct rocket left an 11-health bot falling
into the void; every fixed input fell in replay) and one navigation death
(a rocket's splash on the frame a dodge hop started; its replay lands under
one heading and with the bot's controller). q4ctf1, q4ctf6 and q4xdm11 have
no deaths but miss the simulation gate (33.6, 30.1 and 38.1 s; mean frames
of 245-250 ms while peers compiled) and q4xdm11's client was stopped at the
harness timeout. Across the catalog: 295 kills, 926 completed traversals,
no traversal timeouts, 40 stuck reports, 10 goal give-ups, 10 no-route
reports and 16 refused pickups; CTF and DeadZone objectives score
normally (CTF captures on q4ctf2, q4ctf4 and q4ctf5; DeadZone 2-94 points a
side). Navmesh generation takes a median 1.4 s (14.1 s on q4ctf1 under the
heaviest load); bot think time is a median 2.7 ms per server frame and nav
1.1 ms, with per-frame maxima to 343 ms that coincide with main-loop stalls
of up to 1.5 s - contention, not bot work: the same code measured 0.55 ms
average and 5.9 ms maximum on q4ctf5 in a calmer stretch.

The four-map set at the dodge/pellet/pad-landing build `f8726c46`
(`.tmp/mp02/four-map-r6/`): q4dm8 three deaths, all excluded (rockets and
rails over the void); q4ctf5 three, two excluded and one navigation death
after a **team-mate's** rocket threw a bot off the drop by the north pads
(the policy excludes only enemy impulses); q4ctf3 one navigation death, a
flag carrier holding for func_jumppad_20's corridor that slid into the kill
pit beside it, which `e1e41a06` fixes; q4dz3 no deaths but 72.7 s of
simulation. The final build (`e1e41a06`; the later commits change only the
classifier), four-map set at 180 s (`.tmp/mp02/four-map-r7/`) and the four
catalog variants that had missed their gate or failed, at catalog length
(`.tmp/mp02/catalog-rerun-r7/`), with deaths reclassified under the final
rule (`.tmp/mp02/reclassify-*`):

| Map (mode) | Sim s | Kills | Team score | World deaths | Navigation | Excluded | Stuck / give-up / timeouts | Think avg / max ms | Nav ms | Navmesh ms | Frame mean / max ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| q4ctf3 (CTF) | 152.1 | 12 | 2-0 | 0 | 0 | 0 | 0 / 2 / 0 | 2.82 / 211 | 1.15 | 5603 | 41.1 / 446 |
| q4ctf5 (CTF) | 164.8 | 27 | 3-3 | 1 | 1 | 0 | 5 / 0 / 0 | 1.64 / 120 | 0.38 | 3017 | 23.9 / 422 |
| q4dm8 (DM) | 168.6 | 34 | - | 4 | 3 | 1 | 4 / 0 / 0 | 1.44 / 28 | 0.58 | 754 | 16.7 / 48 |
| q4dz3 (DeadZone) | 168.6 | 28 | 46-76 | 0 | 0 | 0 | 4 / 0 / 0 | 1.02 / 21 | 0.26 | 1522 | 16.7 / 80 |
| q4ctf1 (CTF, 90 s) | 81.9 | 10 | 1-1 | 0 | 0 | 0 | 0 / 1 / 0 | 1.43 / 19 | 0.53 | 1326 | 16.7 / 43 |
| q4ctf6 (CTF, 90 s) | 81.9 | 4 | 1-0 | 0 | 0 | 0 | 0 / 3 / 0 | 1.51 / 19 | 0.65 | 2649 | 16.7 / 96 |
| q4dm8 (DM, 90 s) | 81.9 | 17 | - | 0 | 0 | 0 | 0 / 0 / 0 | 1.22 / 16 | 0.46 | 759 | 16.7 / 106 |
| q4xdm11 (DM, 90 s) | 81.9 | 12 | - | 0 | 0 | 0 | 1 / 0 / 0 | 1.58 / 15 | 0.61 | 660 | 16.7 / 48 |

The last four ran in a quieter stretch (frames at 16.7 ms, 0.7-0.8 cores
busy): about 1.2-1.6 ms of bot think per server frame with 15-19 ms peaks,
0.5-0.65 ms of navigation and 0.7-2.6 s of navmesh generation for six bots
on the debugoptimized build. These are the most useful budget indications
here; they are still not a controlled performance qualification.

Final-build deaths, each with its evidence:

- q4ctf5 Cortez (75.2 s): a rail's kick (5 health left); the replay lands
  under two headings (air06, air09) but not with the bot's own controller -
  a **navigation/control** death the air-input choice did not find.
- q4dm8 Voss (35.5 s): a machine gun's tap on a bot already falling - the
  replay after the tap falls under every input, and so does the moment
  before it. **Navigation**: the counterfactual rule (added after this run
  first excluded it) refuses the exclusion.
- q4dm8 Sledge (54.7 s): rocket splash; one replay trial failed to arm (a
  removed replay bot was still in use), so **navigation** (fail closed).
- q4dm8 Strauss (95.6 s): rocket splash, 51 health; every input falls and
  all three counterfactual trials land. **Excluded** as impulse-caused.
- q4dm8 Strauss (158.1 s): no impulse; a routed 328-unit jump down from
  `520 -571 328` toward `532 -548 0` ended in the void. The mirrored jump
  (`520 702 328` toward `532 676 0`) killed a bot in an earlier rerun and
  lands in isolated replays (`.tmp/mp02/dm8-jump-r1/`), so the match-only
  cause - most likely a collision with another player in the drop - is
  unestablished. **Navigation**.

Earlier runs' reclassified deaths agree: the four-map run at `f8726c46` has
three excluded deaths and three navigation deaths (a team-mate's rocket off
the north drop, a rail kick whose moment-before also falls, and a q4dm8
splash that lands under one heading in the new replay although the first
classification had excluded it - replays at that margin are not always
repeatable), and the catalog's q4dm8 one excluded and one navigation death.

**The zero-navigation-death gate does not pass.** What remains under MP-02:
the air-input choice still misses narrow recoverable windows that sixteen
fixed headings find; team-mates' splash knockback (bots fire rockets beside
team-mates); the q4dm8 jump-down deaths; replay-bot replacement (`removebot`
followed by `addbot` still reports the character in use, which can leave a
trial unarmed); and a catalog sweep on one build in an uncontended window -
29 variants passed at `409cee58` and the four that missed rerun clean at
`e1e41a06`, but no single sweep covers all 33 at the final build.

### Air recovery and replay isolation, 22 October 2026

The follow-through on the recorded CTF5 rail fall now finds the recoverable
landing. Recovery retains a still-safe input, checks sixteen world headings
alongside the eight braking-relative headings, and predicts at the player's
16 ms integration step. If no affordable landing exists, supported floor is
the last resort; a live health pickup can make that otherwise lethal impact
survivable. Pickup eligibility uses the item's actual trigger bounds, contents,
visibility, pickup state and match instance, rather than its smaller model
bounds. This fallback only steers an already-knocked fall; it never approves a
voluntary route with lethal landing damage.

The recorded Cortez state (`593.061 1134.060 182.008`, velocity
`169.541 -380.616 107.461`, health 5) now lands with the controller alive at
50 health. In `.tmp/outstanding/recovery-reset-world-q4ctf5/result.json`, all
three trials arm and the controller, `air06` and `air09` survive. The staged
MP module SHA-256 is
`1242fd199f424a6a09477e43124db5e9b289a20d6f3f26a4356ad2449b03b120`;
the client is
`6c08a406a1856c25f33c4c9490a7515f494d8bc5823d15e1a4e37350d28e5444`.
The source base is `1e259bfc` plus the recovery/replay diff. The native fixture
extracts the production chooser and item-trigger predicate; its five mutation
controls fail when fine headings, stable steering, supported fallback, health
priority or the actual pickup trigger are removed.

Replay isolation also needed repair. Buffered `removebot` now inserts the
server kick before the next command, freeing the character for replacement.
The replay stays in playable warmup, avoiding a match forfeit when its sole bot
is removed. Every trial restarts the map to restore pickups, then reinstates
the observer's god/noclip state. Without that world reset the successful first
trial could take the health that made a later identical heading survivable.
The separate DM8 Sledge replay armed all 21 trials; its controller and 18 other
trials survived, including all counterfactuals. That death is recoverable and
continues to count as navigation, not an impulse exclusion.

These targeted passes do not erase the earlier deaths or establish the cause
of the independent DM8 jump-down failure.

### Single-build complete catalog, 22 October 2026

`.tmp/outstanding/catalog-final-staged/` contains the complete 33-variant sweep,
its per-map reports/logs/launch commands and `manifest.json`. All 33 pass the
strict zero-world-death gate, including warmup: 215 combat kills, zero world
deaths, zero traversal timeouts and no impulse exclusions. Each map records
46.208–48.272 seconds of simulation, above the 40-second minimum. The run uses
six skill-4 bots, a 55-second measured window after five seconds of warmup,
640x480 hidden/windowed Vulkan, Classic UI and installed stock assets. One
client runs at a time, with no concurrent build or runtime test. The selected
valid map modes cover nine CTF, twenty DM and four Dead Zone variants; this is
not every map/mode combination.

The final identity check confirms unchanged tested source and staged binaries
throughout the sweep. The source base is `1e259bfc294ad210b103e1d3b861cab912689c56`
plus the saved `source.diff` (SHA-256
`7c10e135b076647e57ce79e1e0cd69d3ca024a582df1849df641f2e291d30526`).
The final rebuild includes the same recovery behavior as the earlier targeted
replay, with subsequent comment-only C++ edits:

| Staged artifact | SHA-256 |
| --- | --- |
| Client | `d67bfc093c4f5e47233abbf2fd7f117c7f862bf9ab1506e5fca8b4c6089edb7b` |
| MP module | `5e84f1eada554dce0475ebd1291a6a9c9a6d80f7ceceefe64d9c03f226a349aa` |
| pak0 | `504ae18b437d854c4921a8cd958468cbde40f8071d6e70b4a79414f9442361e2` |
| pak1 | `1a7c763fbba69207725774d45139ce5a8583300e4f89a55ecf12760bfb6570a9` |

Selected previously investigated maps from this sweep (all have zero world
deaths; scores are Marine–Strogg):

| Map | Simulation s | Kills | Team score | Bot think mean / max ms | Whole-frame p95 ms |
| --- | ---: | ---: | --- | ---: | ---: |
| q4ctf3 | 48.128 | 5 | 1–0 | 1.315 / 43.852 | 27 |
| q4ctf5 | 48.160 | 6 | 1–0 | 0.578 / 10.832 | 20 |
| q4dm8 | 47.968 | 8 | — | 1.924 / 45.935 | 37 |
| q4dz3 | 48.176 | 11 | 27–26 | 0.759 / 27.995 | 23 |
| q4xdm13 | 48.160 | 11 | — | 0.824 / 16.793 | 20 |

Across the catalog, mean bot think costs span 0.578–2.058 ms per game frame,
the largest recorded think is 80.850 ms, navigation averages 0.156–1.301 ms
per frame and navmesh generation takes 153–3,592 ms. These are observations,
not ratified tail-budget passes. Reports retain 17 stuck events, 12 goal
give-ups, seven no-route events and 19 refused pickups, despite the passing
death gate. The final thirteen bot source/native harnesses and runtime
freshness regression all pass (`.tmp/outstanding/final-checks/`); the death
policy retains its positive control and 21 failing controls.

This completes the outstanding single-build catalog rerun. It does not replace
the earlier long-match evidence or close MP-02's independent DM8 jump-down,
team-knockback and controlled timing requirements, nor broader route recovery
under MP-03. No full-duration four-map or two-hour voice rerun is claimed here.

### Team-knock guard and trench rims, 23 October 2026

Runs use `.tmp/runtime/candidate/`, built from this round's source, which
also carries the networking, demo and prediction changes recorded under MP-01
to MP-08 in the [roadmap](roadmap.md).

**Navmesh reach.** `.tmp/mp-round/nav_trench_audit.py` loads each catalog
variant without bots, runs `navmesh build` and `navmesh info`, and reads the
build summary. All 33 complete with no fatal error. Twenty-four maps gain
trench-rim jumps, 271 in all (3.3% of 8,284 gap jumps):

| Map | Trench-rim jumps | Gap jumps | Build ms |
| --- | ---: | ---: | ---: |
| q4ctf7 | 75 | 689 | 3,853 |
| q4ctf8 | 25 | 484 | 2,599 |
| q4ctf6 | 24 | 638 | 2,633 |
| q4xtourney1 | 18 | 141 | 176 |
| q4dz3 | 17 | 343 | 1,652 |

The other nineteen maps gain one to thirteen each. q4ctf2, q4ctf5, q4dm5,
q4dm9, q4dz1, q4dz4, q4tourney1, q4xctf6 and q4xdm15 gain none. Builds take
176–3,853 ms. Per-map results are in
`.tmp/mp-round/runtime/nav-trench/report.json`.

**Bot audits.** `.tmp/mp-round/run_bot_audits.py` runs
`tools/tests/mp_bot_audit.py` one client at a time: six skill-4 bots,
`bot_debug 2`, 90 seconds, impulse classification and the zero-world-death
gate. It covers the three maps with the most trench-rim jumps, q4dz3, q4ctf5
(where a bot had died to a team-mate's rocket), q4xtourney1 and the liquid lab:

| Map | Mode | Simulation s | Kills | World deaths | Stuck / give-ups | Strong team-mate knocks |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| q4ctf7 | CTF | 81.9 | 5 | 0 | 0 / 0 | 0 |
| q4ctf8 | CTF | 81.9 | 9 | 0 | 4 / 0 | 0 |
| q4ctf6 | CTF | 81.9 | 2 | 0 | 0 / 1 | 0 |
| q4dz3 | Dead Zone | 80.4 | 14 | 0 | 0 / 0 | 1 |
| q4ctf5 | CTF | 71.1 | 10 | 1 (excluded) | 0 / 0 | 1 |
| q4xtourney1 | DM | 81.9 | 11 | 0 | 1 / 0 | — |
| liquid_lab | DM | 81.9 | 35 | 0 | 20 / 0 | — |
| liquid_lab, after the dive change | DM | 81.9 | 22 | 0 | 0 / 0 | — |

Every audit passes the gate. q4ctf5's death was an enemy rocket push: replay
showed every fixed air input and no input falling to the world, and the same
moment without the push landing, so the audit excludes it. A strong knock is
one of at least 150 units/s. q4dz3's was a team-mate's rocket splash at 237
units/s that landed safely and did no damage, which the guard allows by
design. q4ctf5's was a railgun hit at 500 units/s on a team-mate standing
behind an enemy: the multiplayer railgun penetrates players (`penetrate` 1.0)
and the bot's line-of-fire trace stopped at the first one. Bots now trace a
penetrating hitscan's whole range through players, as `idGameLocal::HitScan`
does, and hold fire when a team-mate is on the line
(`BotCombatPenetratingShotHitsTeamMate`).

The liquid lab's 20 stuck reports share one shape: a route dropped from a
walkway onto a node on a pool floor 128 units down, and the bot floated about
45 units above it. `rvBot::UpdateLiquidMovement` held swim-up whenever the
bot's eyes were under water. A swimming bot now dives for a route corner more
than 16 units below it in water while it has more than half its air, and
surfaces otherwise. The rerun on the same build plus that change records no
stuck reports, goal give-ups or no-route events, 28 completed traversals
(8 before), 22 kills and no deaths in 81.9 simulated seconds. The pre-change
run is kept in `.tmp/mp-round/runtime/audits/mp_liquid_lab-DM-before-dive/`.
The railgun change has no runtime capture: it needs a rail through an enemy
into a team-mate, which no audit reproduces on demand.

### q4dm8 recurrence audits, 24 October 2026

Three 140-second q4dm8 audits on the network-qualification build
(`.tmp/mp-round2/run_dm8_audits.py`, six skill-4 bots, `bot_debug 3` so jump
takeoffs and frames are logged) looked for the unexplained jump-down death:

| Run | Simulation s | Kills | World deaths | Excluded (enemy push) | Navigation deaths |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 139.8 | 30 | 6 | 6 | 0 |
| 2 | 139.7 | 21 | 1 | 1 | 0 |
| 3 | 139.8 | 32 | 1 | 1 | 0 |

Every death followed an enemy push whose replay fell under every fixed air input
while the same moment without the push landed. The jump-down did not recur in
419 simulated seconds, so its suspected cause, a collision with another player
in the drop, is still unestablished; no speculative guard was added. Reports
are under `.tmp/mp-round2/runtime/dm8/`.

### Rim landings, dodges, drops and pursuit progress, 26 October 2026

A Codex round on 23 October fixed four navigation causes in its own uncommitted
checkout; its bot changes landed here on 26 October, after main's own
multiplayer round, which had covered other ground.

**Inward rim landings.** Kane's CTF5 death followed an earlier rail hit at
51,216 ms, not the later machine-gun hit the original classifier picked. Its
fixed-input replay found a surviving heading: the air predictor rejected the
first rim contact although inward momentum carried the hull continuously onto
the walkway. Emergency air recovery (`AirInputLandingSafe`) now accepts such a
touchdown when `RecoveryStepClear` finds support along the stopping path, then
checks the rest of that path from the supported point. Lethal floors, transport
volumes, gaps and unaffordable impacts are still rejected, and voluntary jump
admission keeps its own support rules. The team-knock guard asks the same
question of a pushed team-mate, so it now reads such a landing as safe too.

**Interrupted dodges.** An active dodge rechecks its actual flight at most once
every 96 ms. When its landing is no longer safe, it releases the takeoff input,
clears the obsolete route and hands over to air recovery. DM8's original
rounded dodge diagnostics could not reproduce the exact departure, so logging
now keeps three decimal places. Native tests exercise the handoff and reject
disabling it.

**Knockback classification.** Sledge's CTF5 death followed an earlier rocket
at 67,600 ms: all 18 post-hit steering and controller trials died, and all
three counterfactuals landed. The classifier in `mp_bot_impulse_replay.py` now
searches up to eight earlier enemy hits, but only once every trial for the
later hit armed and died. Each earlier hit needs the same complete exclusion
proof; a survivor, an unarmed trial or an incomplete run stops the search and
keeps the death a navigation failure.

**Planned drops.** Voss's CTF5 stall after the rail hit at 52,144 ms
reproduces from the recorded pose and momentum. The walking edge guard kept
cancelling the planned drop, because its near-term footprint straddled the
source lip; a drop from rest at the aligned entry also stalled. The drop now
overrides the brake only when a fresh trajectory from the bot's actual
position, velocity and input proves an affordable, supported landing with room
to stop (`rvNavMesh::DropTakeoffClear`). Unplanned transport volumes stay
excluded, and an active knockback slide still brakes. When a full-speed
departure would overshoot onto a pad, the controller also tries a quarter-speed
ground approach. If both proofs fail, the brake stays.

**Pursuit progress.** Strauss's interception give-up exposed a progress
accounting gap: refreshing a moving target's route kept the shortest distance
from older routes, so progress along a longer replacement went unrecognised.
A successful replan now rebases that distance while keeping the progress clock
and any movement still below the progress threshold. Replanning alone never
extends the give-up deadline. Native controls cover ordinary and slow moving
pursuers, a stationary pursuer that must still give up, and failed refreshes.

`tools/tests/mp_bot_rim_replay.py` replays the low-health CTF5 rail hit with
the controller and the fixed-input controls. `tools/tests/mp_bot_drop_replay.py`
replays the recorded post-hit state, the rounded settled pose and the aligned
entry through real player physics.
`bot_probeDrop client x y z vx vy vz targetX targetY targetZ [entryX entryY entryZ]`
uses the isolated local cheat-server restrictions of the other probes; the
optional entry keeps the route's source while it replays a displaced bot.
`mp_bot_audit.py` no longer reports `com_speeds`' `gfr` as multiplayer game
time: the counter times the session loop, not the asynchronous server, and is
kept as `session_game_frame_counter_ms`.

On the landed build (six skill-4 bots, `bot_debug 3`, impulse classification,
zero-world-death gate), a 120-second q4ctf5 CTF audit simulated 108 seconds with
10 kills, no world deaths (warm-up included), and no stuck events, goal give-ups
or traversal timeouts; a 120-second q4dm8 audit simulated 111 seconds with 21
kills, no world deaths, no stuck events, give-ups or timeouts and one no-route
report. `mp_bot_drop_replay.py` arms and completes all three drop trials with no
stall or death, and `mp_bot_rim_replay.py`'s controller trials survive. One run
of each on a shared machine; no A/B against the earlier module was taken, and the
strict long-match gate remains open.

For a live look, run a listen server with `bot_debug 2` and read the status
lines: each carries the bot's position, health, armour, goal, path progress,
current enemy and whether it is firing. Damage and voluntary-jump lines include
position, velocity and game time so a fall can be traced to the preceding
input or hit. `bot_debug 3` adds edge-braking origins, lookahead positions, floor
contacts and rejection reasons, plus exact accepted takeoffs and traversal-frame
commands. `bot_debug 4` additionally logs each live predicted jump frame and
ordinary walking frames, dodge landing decisions and airborne proof rejection
reasons. `bot_debug 5` adds each predicted airborne collision plane.
`bot_probeMove` accepts the same coordinates as
`bot_probeJump` but installs an ordinary walking leg for isolated impulse
investigations, under the same local-server restrictions;
`navmesh probe` reports the full-hull and support-
box floor/continuation verdicts at the requested point. Positive self/team/world damage is included;
`BOT_IMPULSE` separately records all applied player knockback, including zero-health-damage
impulses that are not forwarded by the damage callback. `BOT_TRANSPORT_GUARD` and
`BOT_AIR_RECOVER` (also `bot_debug 2`) mark the unplanned-pad guard and the airborne brake.
`bot_probeAir client x y z vx vy vz dirX dirY dirZ` flies one fixed full air input with no
route or controller and brakes once grounded, under the same isolated-server restrictions;
every probe reports `BOT_PROBE_GROUNDED` at its first real touchdown. `bot_debugNav 2` draws the mesh and
every bot's route near the local player, and `bot_debugAim 1` logs the
reaction, settle and lead behind every shot.

`tools/tests/mp_bot_gameplay_smoke.py` remains the repeatable stock-gameplay
check for DM, Team DM and CTF: live-match admission, bot movement, firing,
damage/scoring and CTF objective decisions, with bot message-queue overflow a
failure.
