# Rigid-body and ragdoll physics reference

This is the reference for the PHYS track in the [roadmap](roadmap.md#rigid-body-and-ragdoll-physics):
the legacy baseline, the enhanced simulation behind `g_enhancedPhysics`, the
contract both modes keep, the budgets and tolerances, and the evidence. The
roadmap owns scheduling; this file records what the code does and what the
measurements show.

## Contract

**One option.** `g_enhancedPhysics` (archived, boolean) is the only player
control. Game Options > Gameplay And View > Enhanced Physics (`#str_231720`,
twelve languages) sets it on the stock and retained pages. The value is
committed once per map in `idGameLocal::LatchPhysicsMode`, called from
`LoadMap` before any entity spawns, so a change applies from the next map or
saved game and never switches solvers under moving bodies. The log line
`physics: legacy simulation` or `physics: enhanced simulation` records the
committed mode.

**Multiplayer follows the server.** A server (listen or dedicated) commits its
own `g_enhancedPhysics` and publishes the result as the read-only serverinfo
key `si_enhancedPhysics`. Clients and MVD playback commit the published value
(`physics: ... simulation (from server)`) and ignore their own setting, so
networked bodies and client-local corpses and debris run the same solver on
every peer.

**`0` is legacy, unchanged.** Every evaluation dispatches in the
`idPhysics_RigidBody::Evaluate` and `idPhysics_AF::Evaluate` wrappers: legacy
mode runs `EvaluateLegacy`, which is the previous `Evaluate` body with only its
name changed. Nothing the enhanced mode adds is saved or networked, so save
bytes and snapshots are those of the legacy code. A legacy run of the same
workload on the pre-change binary and on the current one produces identical
state digests (see Evidence).

**Developer controls** are cheat-protected: `rb_enhanced*` for rigid bodies
and `af_enhanced*` for articulated figures (iterations, margins, slop,
recovery speeds, joint correction and recovery, impact friction, sleep
thresholds, the impulse guard, inheritance), plus `rb_enhancedDebug <entity>`
for a per-step trace and `af_enhancedDebug` (1 the energy guard, 4 each
exact-solver fallback, 8 every contact after the solve).

## What the enhanced mode does

Shared code lives in `src/game/physics/` and is compiled into both game
modules: the solver kernels in `EnhancedSolver.h` (plain float math, driven
directly by the native test), the rigid-body step in `EnhancedRigidBody.inl`,
the articulated-figure step in `EnhancedArticulated.inl`, telemetry in
`PhysicsTelemetry.h` and the measurement commands in `PhysicsCommands.inl`.

### Rigid bodies (PHYS-02)

Applies to passive bodies: `idMoveable` and its subclasses, moveable items,
brittle-fracture shards and client-side debris (brass, gibs, chunks).
Projectiles (grenade bounces are gameplay), vehicles, AI that fly on
rigid-body physics (the convoy hover tank) and bound bodies keep the legacy
step in both modes. Placement (`DropToFloorAndRest`) and resting bodies use
the legacy path in both modes.

1. Semi-implicit integration of gravity, external forces, liquid drag and
   buoyancy, with the authored linear and angular friction as implicit decay.
2. One translation sweep along the motion (`idClip::Contacts` with the step's
   travel plus `rb_enhancedMargin`) is both the collision test and the contact
   query. Each contact's gap is exact. The collision model names the pair of
   features that would meet (a vertex of the body on a polygon, an edge on an
   edge, or a vertex of the obstacle on a polygon of the body), and the gap is
   measured between those features where the body is now
   (`EnhancedContacts.h`, shared with figures; `rb_enhancedFeatureGaps 0`
   restores the old estimate for comparison). Until 16 November the gap was
   estimated from the body vertex nearest the sweep's line, which on faceted
   ground picked the wrong vertex (see
   [Contact gaps](#contact-gaps-from-the-collision-models-features)).
3. A persistent manifold keeps surfaces from recent steps that the sweep no
   longer reports. Planes are stored in the touched body's frame, so a body
   resting on a jostled neighbour cannot fall through it once their sweeps
   start inside each other. A kept contact is measured again on the feature
   that made it; when that feature has left the plane, on the lowest vertex
   near the contact point.
4. Warm-started sequential impulses with Coulomb friction, rolling and
   spinning resistance, and speculative gaps (a gap closes to the slop within
   a step, never past it).
   - **Friction is μ = 3 × the body's authored contact friction**, clamped to
     0.05..1.2 (`rb_enhancedFrictionScale`): the stock 0.05 is 0.15 and a
     crate's 0.2 is 0.6. Legacy's contact friction is not a coefficient: each
     contact removes that fraction of the sliding velocity every step, so a
     body knocked loose slides a distance that grows with its speed, not its
     speed squared. The factor is the one at which pushed crates and barrels
     slide as far as legacy's on a flat floor (see
     [Friction calibration](#friction-calibration)). Until 16 November it was
     12, which stopped them in a third of the distance.
   - **An impact slides and bounces.** A contact the body approaches faster
     than max(20, 2 g dt) units/s and reaches within the step is an impact. It
     has no friction (`rb_enhancedImpactFrictionScale` 0) and rebounds by the
     body's bouncyness. That is legacy's collision impulse: along the normal
     only, with the authored bounce. A new touch always rebounds. A contact
     the body already had rebounds too (`rb_enhancedBounceResting` 1) when it
     is with fixed geometry and the body touches no other moving body: a
     splash shoves a resting crate into the slope it stands on, and with full
     friction on that impact, and no bounce because the contact was not new,
     the crate lost its speed where legacy's skipped up the slope. In a stack
     that rule does not apply. The weight of the bodies above reaches the one
     below as reactions, which arrive as impacts on its support; rebounding
     off them kept the flat-floor stacks hopping for as long as they were
     awake (325 ms of physics against legacy's 88, and still moving after 19
     seconds). Slower touches keep full friction and do not bounce, so
     resting and sliding bodies stop as before.
5. Overlap is removed by a pseudo velocity that moves but never accelerates
   the body (bounded to 40 units/s); overlap deeper than 4 units is removed by
   position only.
6. Between two awake enhanced bodies each contact is solved once: the body on
   top owns a stacking contact, the lower entity number a side contact. A
   contact with another dynamic body shares the impulse by both masses and
   hands the other the reaction; a sleeping support under a resting load is
   treated as fixed and left asleep.
7. The position step stops at the time of impact; a turn that sweeps a
   body's extent by more than `rb_enhancedRotationSweep` is swept.
8. An energy guard limits any kinetic energy gain to the work gravity and
   external forces did over the step.
9. Bodies sleep after `rb_enhancedSleepFrames` (10) supported steps below
   `rb_enhancedSleepSpeed` (1.5 units/s) and `rb_enhancedSleepAngularSpeed`.
   Until the third round the window was 20 steps at 3 units/s. The sloped
   stacks then took 1,988 body steps to settle against 879, and a crate or
   barrel creeping down the crater at 2 to 3 units/s was put to sleep
   part-way: at 3 units/s with the short window the Dark Matter splash's
   median travel there fell to 38 units against legacy's 133. Two other rules
   were measured and dropped: letting a body woken by a neighbour sleep again
   after two quiet steps (432 sleep and wake cycles in the explosion scene,
   and 40 % more body steps, because a sleeping neighbour takes a push as a
   fixed wall), and restarting the window whenever a body had crept a tenth
   of a unit (46 % more body steps in the same scene). A body
   that has not moved since it was created also rests as soon as it passes
   legacy's own test: three contacts around the centre of mass and almost no
   motion (`rb_enhancedRestTest`). A body placed in a map passes that test on
   its first step and never simulates, in either mode. Until 16 November the
   enhanced step waited for the quiet window alone; in that window a pickup
   balanced on its perch lost the support contacts its sweep no longer
   reported, tipped over and fell out of reach (see Evidence). A body that
   has been knocked about waits for the quiet window: resting it by legacy's
   test put a settling pile's members to rest and woke them again every few
   steps (293 wakes in 20 seconds on the sloped stacks, against 8). A quiet
   body does not wake its neighbours, so a settled
   pile goes to sleep together instead of waking itself every frame; a woken
   body (any impulse, force, push or script move goes through `Activate`)
   counts again.

The impact callback (`Collide`: bounce sounds, impact damage, break and
explode on impact) fires for the strongest approaching contact of a step, as
legacy fires it for its swept collision.

### Articulated figures (PHYS-03)

Applies to every `idPhysics_AF`: AI and player ragdolls, scripted and placed
figures, chains and lights. Stock AF defs are read as authored. Tree joints
are still solved exactly by `idAFTree` in linear time.

1. Speculative contacts per body (reach = the body's travel plus
   `af_enhancedMargin`, at least legacy's 2 units), with exact gaps
   (`af_enhancedFeatureGaps`) and the persistent manifold as above. An edge of
   a body lying across an edge of the world is held at the world's edge:
   until 16 November the row acted at the nearest vertex of the body, which
   for a limb across a ledge is its end, out over the drop.
2. The dense LCP over auxiliary rows (limits, contacts, joint and contact
   friction, loop joints) becomes a warm-started projected Gauss-Seidel solve
   on the same constraint matrix. Contact multipliers are matched to last
   step's contacts; limits and friction keep theirs. When the result's LCP
   error stays above `af_enhancedFallbackError` the exact legacy LCP solves
   the step instead (counted as a solver fallback).
3. **Impacts slide.** A contact the body approaches faster than the bounce
   threshold and reaches within the step is an impact, and an impact has no
   friction (`af_enhancedImpactFriction` 1 with
   `af_enhancedImpactFrictionScale` 0, the defaults). Legacy resolves impacts
   without friction: its collision impulse never changes the figure's own
   velocity and its closing-velocity check is normal-only. With full Coulomb
   friction on an impact-sized normal force, a strogg marine (contact friction
   0.8) splashed into Air Defense 1's rising crater floor lost about 95% of
   its momentum in one step (median travel 30 units against legacy's 247).
   From 14 to 16 November an impact's friction was bounded by the body's
   resting load instead; a thrown corpse touches down on several bodies at
   once, each with up to three contacts, and that still took a third of a
   rocket splash's speed in the first six steps (see Evidence). The scale
   keeps that bound available: 1 is one resting load per contact. Resting
   contacts and slow touches keep full Coulomb friction, so resting and
   sliding bodies still stop as authored.
4. Overlap, and violated cone and pyramid limits, are removed by a pseudo
   velocity through the same tree responses (so joints stay consistent),
   bounded by `af_enhancedMaxRecovery`.
5. **Joints close without adding energy.** Legacy corrects every joint's
   position error with up to 256 units/s of real velocity, which is what
   turns a limb caught in a wall into a figure that flies apart. Here a joint
   that holds is corrected at no more than `af_enhancedMaxJointCorrection`
   (32 units/s). A joint pulled apart (a body stopped by geometry while the
   rest of its figure moved on) gets more in two ways. Its velocity bound
   rises to legacy's 256 between a quarter of `af_enhancedJointStretch` (8
   units, the stretch that shows as a stretched limb) and the whole of it,
   under the energy guard; that velocity is what slows the figure when the
   stopped body cannot follow. And what the joint needs beyond that bound, up
   to half its gap per step and `af_enhancedMaxJointRecovery` (1024 units/s),
   is closed by position only: the tree is solved once more with no forces
   and no velocities, only that excess as the target, and the resulting
   pseudo velocity moves the bodies together and is discarded
   (`EnhancedJointRecovery`). Position-only motion is not resisted by contact
   friction, so it is used only on joints more than a quarter of the visible
   stretch apart. Until 16 November the 32 units/s bound stood alone, and a
   figure thrown by a splash stayed visibly stretched longer than legacy's
   (see Evidence).
6. **Position-only motion cannot enter geometry.** The recovery of overlap,
   limits and joints moves bodies without the velocity rows that hold real
   motion back. Three things keep it out of walls. The recovery solve
   includes every contact the step knows about, not only the overlapping
   ones, with the joint recovery's motion as its starting point: a gap may
   close to the slop and no further. A body that touches a surface takes no
   position-only motion into it. And because a surface the contact query did
   not report holds nothing back, the position-only part of a body's motion
   is swept on its own after the real motion (`af_enhancedRecoverySweep`, for
   any such move over 0.1 units), so stopping it never stops the body's real
   motion; a body being pushed out of an overlap is exempt, since a sweep
   that starts inside a surface cannot move. Before 16 November limit
   recovery was solved with only the overlapping contacts, and small
   position-only moves were never swept: about one corpse in six killed
   against a wall came to rest with a limb a few units inside it (see
   Evidence).
7. **A surface that stops a body is solved against.** A body's sweep can be
   stopped by a surface its contact query did not report: an edge, or a
   surface off its line of motion. Without a contact row the body keeps its
   velocity into that surface, is stopped again every step, and the rest of
   its figure pulls away from it; this was the longest joint stretch in both
   modes. The stopping surface now joins the manifold
   (`af_enhancedCollisionContacts`), so the next step's solve includes it.
8. Figures do not bounce, as in legacy. Legacy wrote the bounce term with the
   opposite sign, so a stock figure never bounced, and the stock AF defs were
   tuned against that. Bouncing by the bodies' bouncyness (traced rebounds of
   about 0.4 of the approach speed) sent Dark Matter splashed corpses back off
   the crater walls they slid along in legacy: median travel 58 units against
   legacy's 247, and 165 without the bounce. `af_enhancedBounce 1` restores it
   for experiments.
9. Only bodies that move or turn beyond their speculative reach are swept;
   legacy sweeps every body every step, which is most of what it costs. Two
   things that legacy's sweeps did are kept another way:
   - **A body placed inside geometry stays there.** Maps hang lights with
     chain links inside beams, and monsters die with an arm in a wall. A
     contact query reports nothing for a surface the body is already behind;
     legacy's sweep, starting in the surface, cannot move further into it,
     which is what makes Putra's lights stiff. When a figure wakes, each body
     is tested against the world and scenery that never moves
     (`EnhancedBodyEmbedded`); one that starts inside is not pushed out by
     overlap recovery (`af_enhancedEmbeddedSweep` 1; 2 also sweeps it every
     step). Before 16 November recovery pushed such links out of their beam
     and two of Putra's lights came loose.
   - **A creeping body is audited.** The contact query is one sweep along the
     velocity the step started with; the solve can turn the body another way,
     and a small move off that line goes through space nothing looked at. A
     corpse jittering at a few units a second walked its torso through a thin
     sheet that way, a tenth of a unit a step. Every `af_enhancedAuditFrames`
     (8) steps a body that was not swept is swept once along the chord from
     the pose it was last verified at to where it is now, if that chord is two
     units or less, and put back where the chord meets a surface. The audit
     acts only when the body is more than the slop past that point (a resting
     body sits a slop from its support, as close as a sweep lets anything
     come) and is not an impact: nothing is called back and nothing is
     pushed. Sweeping every stray move instead was measured and dropped: it
     quadrupled the sweeps and stopped limbs in flight (see Known defects).
10. The energy guard limits kinetic energy as for rigid bodies, but takes the
   excess from the motion relative to the figure's centre of mass first,
   which joint and contact forces can change; only an excess larger than
   that also scales the shared motion. Scaling every body uniformly also
   removed centre-of-mass momentum that no internal force can change.
   Quiet-frame sleeping and quiet-figure wake rules are as for rigid bodies
   (sleeping still requires the figure to allow rest).
11. A ragdoll whose def sets no `velocityTime` inherits the last
   `af_enhancedInheritTime` ms of its animation's joint motion plus the
   entity's own velocity (capped per body by `af_enhancedMaxInheritSpeed`),
   instead of starting from rest.

### The impulse guard (PHYS-04)

Every impulse on a figure keeps its legacy linear momentum J. When the struck
body would gain more than `af_enhancedImpulseGuard` (300 units/s), a fraction
of J is applied at the hit point and the rest is spread over every body by
mass (`OpenQ4Physics::SplitImpulse`). The struck body's change never exceeds
the legacy J / m; a hit whose share per unit mass already exceeds the guard is
spread fully. Clamps are counted (`impulse_clamps`) together with the largest
struck-body change (`peak_impulse_dv`) and peak body speeds.

## Measurement tools

| Tool | Use |
| --- | --- |
| `physicsStats start [label] \| stop \| report \| frames` | Per-frame CPU samples for rigid bodies and figures; prints one `PHYSICS_STATS {json}` line with totals, median, P95, P99, the worst frame, evaluation and motion counts, sleeps, wakes, sweeps, energy clamps, solver fallbacks and the time spent in them, impulse clamps and peaks, the largest joint stretch and the frames in which any joint was more than 8 units apart. `frames` prints every captured frame's cost (`PHYSICS_FRAMES`). Evaluations over 10 ms print `PHYSICS_SLOW`; rigid bodies over 2000 units/s print `PHYSICS_FAST`. |
| `physicsDigest [verbose]` | FNV-1a hash of every rigid-body and figure state, and separately of the workload's own `phys_*` entities (map AI and scripts can move map-placed bodies differently between runs). |
| `physicsReport <prefix \| * \| @debris>` | Mass, mass-weighted centre, momentum and peak body speed per entity; `*` is every entity, `@debris` the client-side debris. |
| `physicsQuality <prefix \| *> [eye]` | Per entity: the largest joint anchor separation (stretch), the joints with static geometry between a body's centre and the joint (pierced: a limb through a wall or floor, with a `PHYSICS_PIERCED` line naming the joint; a surface in the gap between a body's box and its joint counts only when it separates the two bodies or reaches more than two units past the joint), the bodies whose centre is inside a solid brush (embedded), whether it is awake and its fastest body. With `eye`, also whether static geometry hides it from the player's eyes. |
| `physicsWallProbe <yawFrom> <yawTo> <step> [pitch]` | Distance, thickness and incidence of the first static surface along each direction from the player's eyes: where to aim a tunnelling repro. |
| `physicsRest <prefix>` | Cheat: puts every rigid body and figure whose name starts with the prefix to rest where it is and prints `PHYSICS_REST` with the count. The workloads use it on Air Defense 1's placed ragdolls (see [Budgets](#budgets-and-tolerances)). |
| `physicsSpawn`, `physicsLaunch`, `physicsDamage`, `physicsRadiusDamage`, `physicsImpulse` | Cheats that stage scenes from the console. `physicsSpawn` places entities in a ring, line, grid or stack, or with their backs to the wall ahead (`wall`), and skips a placement the static world blocks instead of moving it onto its neighbours. `physicsLaunch` fires one entity along the view at a given speed and prints the surface it is aimed at. `physicsImpulse <prefix> * ...` reaches every body a prefix names. |
| `tools/tests/physics_workloads.py` | Hidden, silent, one tic per frame (`com_fixedTic 1`), console-driven reference workloads in either or both modes. `--frames` records every frame's cost and reduces repeats to a per-frame noise floor; `--rest PREFIX` rests the map's own bodies of that name before measuring; `--cache-root` shares the generated-image cache between runs. |
| `tools/tests/physics_enhanced_solver.py` | Native test of the production kernels and telemetry (CI). |
| `tools/tests/physics_defects.py` | PHYS-01 defect repros in stock maps, both modes. The stuck-limb repro pools three stagings (`--stagings`). |
| `tools/tests/physics_envelope.py` | PHYS-04 behaviour envelope: `--scene ai`, `moveables`, `moveables-flat`, `crates-flat`, `barrels-flat`, `mp` or `awakening`. |
| `tools/tests/physics_coverage.py` | PHYS-04 for bodies held by the world and for client debris. |
| `tools/tests/physics_campaign.py` | PHYS-04 for the stock campaign: the 31-stage level chain and the scripted set pieces, both modes. |
| `tools/tests/physics_integration.py` | PHYS-05 saves, client, MVD and dedicated checks. |

### Reference workloads

| Workload | Scene |
| --- | --- |
| `sp-ragdolls` | Eight strogg marines in a ring around the player, killed by machinegun and rocket-splash hits through the normal Damage path, then left to fall and settle. |
| `sp-explosion` | A grid of crates, a ring of barrels and an exploding barrel; a rocket splash sets the barrel off. |
| `sp-stacks` | Crate and barrel stacks on Air Defense 1's sloped crater floor, nudged from below. |
| `sp-flatstacks` | The same stacks on Hangar Perimeter's flat floor; they must stand. |
| `sp-setpiece` | Air Defense 1's grunt wrestle: the map's `gruntBlasted` kills the grunt and applies its two scripted impulses. |
| `mp-frags` | A seven-bot deathmatch on `mp/q4dm1` on a loopback listen server; corpses, gibs, brass and dropped weapons. |
| `sp-visual` | The ring of marines dies around the player and is photographed from the player's eyes in three directions at fixed moments, then thrown by a grenade splash and photographed again. |
| `sp-visual-rigid` | The sloped-floor stacks, nudged and then hit by a rocket splash, photographed the same way. |

### Noise floor

The machine these runs come from is shared, and a deterministic workload does
the same work in the same frame of every run. A frame's cost is therefore the
smallest reading it shows over repeated runs; anything above that is
scheduling, caches or another process. `physics_workloads.py --frames` records
every frame, and the summary's `floor` gives the total, P99 and worst frame of
the per-frame minimum, with the readings the worst frame showed in each run.
The worst-frame budget is judged on the floor. Multiplayer workloads are not
deterministic and keep medians.

## Legacy impulse envelope

What each damage path delivers today; enhanced mode keeps the momentum of
every one of them.

| Path | Code | Impulse (stock defs) |
| --- | --- | --- |
| Direct hit on an entity | `idEntity::Damage` → `ApplyImpulse( dir * push )` | the def's `push`, 5000 by default (machinegun bullets carry none of their own) |
| Death push | `idActor::DeathPush`, every tic for `deathPush` ms at the root body (or `deathPushJoint`) | machinegun 3000-5000 horizontal, 350-500 up for 100 ms; gauntlet 2000-4000 / 400-500 for 200 ms; rocket direct 3000-5000 / 2000-3000 for 200 ms; grenade and Dark Matter splash 7000-10000 / 9000-12000 for 100 ms; plus half a tic of gravity |
| Splash | `idGameLocal::RadiusPush` → `RadiusPushClipModel`: every clip model in range, so every body of a figure, gets the full push | rocket splash 10000 (radius 180), grenade splash 10000 (250), Dark Matter splash 20000 (150), hyperblaster splash 0 |
| Projectile impact | `idProjectile::Collide` | the projectile def's push along its flight |
| Hitscan | `idGameLocal::HitScan` | the hitscan def's `push` (5000 by default) at the hit point |
| Melee | gauntlet as a direct hit plus its death push | as above |
| Vehicle impact | `rvVehicle` collisions, moveables only | push × speed scaling, up 0.75 × push |
| Scripts | `applyImpulse` | Air Defense 1's grunt blast: 20000 at the head and 30000 at the origin |
| Corpse sink | `g_corpseSink` | moves corpses into the floor without an impulse; unaffected |

## Budgets and tolerances

- **CPU.** On each reference workload the enhanced mode's aggregate physics
  CPU time (rigid bodies plus figures) must not exceed legacy's, and its
  worst frame must not be worse. Deterministic workloads are judged on the
  [noise floor](#noise-floor) of repeated runs; multiplayer workloads on the
  median of repeated runs. Any exception is declared here with its bound
  before promotion.
- **The map's own ragdolls rest first.** From 16 November the Air Defense 1
  workloads are judged with the map's nine placed ragdolls put to rest before
  the measurement starts (`--rest env_ragdoll`). Left alone they settle during
  the measurement for a time that depends on the compiler in both modes, and
  that time was most of every Air Defense 1 total: a figure moved in about 900
  steps of a legacy workload on Windows and 131 on Linux, and in 50 and 413 of
  an enhanced one. It decided the comparison in opposite directions on the two
  platforms and said nothing about the bodies each workload stages. The
  unrested runs are still made and recorded.
- **Proposed CPU exceptions (16 November 2026; not accepted; out of date for
  Linux).** On the Linux build with exact math (17 November) `sp-ragdolls`
  costs 65 % more than legacy, past the 25 % proposed below, and
  `sp-flatstacks` 4.1 ms more instead of 8.2. Four measured
  overruns remain after the third round. The budget lets an exception be
  declared with its bound before promotion; these are the bounds that would
  cover what was measured, written down for the project owner to accept or
  reject. Until one of those happens the CPU budget counts as not met on the
  workloads named. The measurements are in
  [CPU on the reference workloads](#cpu-on-the-reference-workloads-phys-02-phys-03-phys-06).
  1. `sp-flatstacks`, total: up to 10 ms above legacy over the workload
     (measured 2.8 ms on Windows and 8.2 ms on Linux, on totals of 12 and 10
     ms). Legacy rests a placed stack on its first step; enhanced keeps each
     body awake for its quiet window and re-wakes neighbours, so the nine
     bodies take 425 to 538 steps against 145 to 164. The stack stands in
     both.
  2. `sp-ragdolls` on Linux x64, total: up to 25 % above legacy (measured
     21 %, 16.6 ms; 15 % below legacy on Windows). The three staged corpses
     take 461 moving steps to settle against 339 with GCC, and 340 against 332
     with MSVC; a step costs the same in both modes.
  3. `sp-explosion` on Windows, total: up to 5 % above legacy (measured 3 %,
     2.2 ms; 26 % below on Linux), and worst frame up to 1 ms above (measured
     2.68 ms against 1.80, the frame in which the 13 staged bodies take their
     first step and enhanced builds their contact manifolds).
  4. Worst frame on any workload: up to 0.05 ms above legacy where both are
     under half a millisecond (measured 0.41 against 0.37 on `sp-setpiece`
     and 0.14 against 0.12 on `sp-flatstacks`, both on Linux).
- **Replay.** Legacy mode must reproduce the pre-change binary's workload
  digests exactly. Each mode must reproduce its own digests between runs on
  single-player workloads (multiplayer bots are not deterministic).
- **Envelope.** Momentum within 2 %; light hits within the legacy
  displacement envelope (largest legacy displacement + 25 % or + 24 units,
  whichever is larger) and a peak body speed within the same kind of bound
  (legacy peak x 1.25 + 24 units/s); heavy hits with a median displacement
  within 0.4x-1.6x of legacy, or both under 32 units. A hit that gibs every
  corpse in both modes passes, since both modes treated it alike and nothing
  is left to compare.
- **Stagings.** A thrown or still-falling figure tumbles chaotically: the
  same hit on a ring two paces wider sends the same corpse somewhere else in
  either mode, and over six solver variants measured on 16 November the
  median travel of one staging of six corpses moved by up to 40 % with no
  change in the mean. The heavy and falling classes are therefore judged on
  three stagings pooled (ring radius 96, 104 and 112: 17 or 18 bodies). A
  resting corpse that a light hit does not move is judged on one. The
  multiplayer scene cannot be staged: each run draws its own spawn points,
  and about one corpse in twenty tumbles 200 to 500 units down the map in
  either mode. From the third round its falling classes are pooled over six
  repeats, after a run of three failed one of them on that draw alone.
- **One start for both modes.** A hit on resting corpses is only the same
  hit if the corpses lie in the same place. From 17 November the Strogg
  scene is staged once, in legacy mode: the ring is killed and
  left to settle, the game is saved, and that save is loaded and hit in each
  mode. Before that each mode killed and settled its own ring, and enhanced
  corpses, which fall on from the motion they had, ended closer to the
  player: four of seventeen lay 20 to 27 units from the blast against
  legacy's nearest at 31, and a corpse that close is pushed into the ground
  in either mode. The still-falling classes cannot share a start and are
  staged as before; so are the multiplayer scene, which cannot be saved, the
  crate and barrel scenes, where nothing moves before the hit, and The
  Awakening's scene, which is judged on the bodies the blast reached.
  `--own-start` runs the old way.
- **Falling light hits.** A corpse hit while it is still falling travels
  mostly by its own collapse down the slope, not by the hit. The largest of
  those collapses is a poor yardstick: legacy's own largest grows from 69
  units on one staging to 91 on three. Its displacement bound is the largest
  legacy displacement + 50 % or + 48 units over the pooled stagings. The peak
  speed bound is unchanged.
- **Joint stretch.** A joint whose anchors are more than 8 units apart shows
  as a stretched limb. Enhanced mode must not keep joints that far apart for
  longer than legacy does on the same hit (4 frames of slack), and its
  largest stretch must stay within 1.5x of legacy's or 8 units more: one
  caught limb sets the peak in either mode.
- **Defect repros.** Enhanced mode must be no worse than legacy on every
  repro measure, and may never leave a body behind the surface it was fired
  at or outside the world. Limbs left in geometry are counted over three
  stagings of six corpses each: one staging is a draw.
- **Pierced joints.** A joint counts as pierced when static geometry lies
  between a body's centre and the joint, more than a unit from the joint.
  From 16 November a surface that lies outside the body's own box, in the gap
  before the joint, counts only when it separates the two bodies or reaches
  more than two units past the joint. A strogg marine's neck joint is 10.6
  units from the centre of a head whose box reaches 7.6 at most, and a ridge
  of terrain that pokes 1.1 units into that gap under a corpse lying face
  down touches neither body: it was counted in three of this round's first
  runs, in two different corpses.
- **Settled before a hit.** A hit on a resting corpse is staged four seconds
  after the kill. On The Awakening's trench one grunt of the ring dies on the
  edge and is still falling then in both modes (22 units/s in legacy, 63 in
  enhanced), so that scene waits ten seconds.

History of the tolerances: the light-hit speed bound was "never above
legacy" until 14 November (a corpse still falling when it is hit, or one that
rolls once on a weak bump, peaked at 122 and 42 units/s against legacy's 103
and 26, which is noise at those speeds, not a flung body). Stagings, the
falling bound and the stretch rule date from 16 November, as do judging a
heavy hit on the bodies it reached, the six multiplayer repeats and resting
the map's ragdolls before a CPU measurement. The common start for hits on
resting corpses dates from 17 November.

## Evidence

Three rounds. The first (14 November 2026) built the enhanced step; the
second (15 and 16 November) widened the testing and fixed what it found in
figures and placed pickups; the third (16 November) fixed the defects the
second left open, as measured on Windows, and then found three checks
failing on Linux x64 that pass on Windows (a splash class on resting corpses,
a placed figure on a ledge and, by 0.09 units, a joint-stretch bound on
Putra's lights). The fourth (17 November) traced those three to the Linux
build itself, which compiled approximate square roots where Windows compiles
exact ones, and to how the envelope staged its corpses; see [the Linux build
was running on approximate math](#the-linux-build-was-running-on-approximate-math-fourth-round).
The tables are the third round's unless a line says otherwise.

Windows x64, RTX 4060 Laptop, debugoptimized build of the third round's
sources, measured before they landed as snapshot `c45be1f81` on top of
`1a60c6c80` (the binaries report the commit they were configured at),
hidden window, silent, OpenGL, one tic per frame. The machine is shared;
most runs had two or three of these clients going at once, and CPU figures
use the noise floor. Linux and ARM64 figures are in their own section. Raw
harness outputs were local scratch and are not kept in the repository; every
harness named here reproduces its table.

### Legacy replay

`g_enhancedPhysics 0` on game modules built from the PHYS-01 commit (the
mode switch, telemetry and workloads, before any enhanced solver code) and on
the 14 November modules, with the same `physicsSpawn` staging in both:
the workload digests (`phys_*` bodies) of all five single-player workloads
were identical at every checkpoint. The all-bodies digest also covers
map-placed bodies that map AI and scripts move; it differs between two runs
of the same binary too, so it is not evidence either way.

After each round the build's legacy digests were compared with the ones
recorded on 14 November: identical on all five workloads at every checkpoint,
the third round's included. Every solver change is inside the enhanced
branches; the shared wrappers gained only telemetry. Each mode repeats its own digests over five
runs on Windows and three on Linux x64 with the map's ragdolls at rest. Of
the three unrested Windows runs one enhanced `sp-explosion` run started two
game frames earlier than the others and differs from them (see the CPU
section). Windows and Linux digests differ from
each other in both modes, legacy included (MSVC and GCC do not round the same
way), so no digest is claimed across platforms.

What the digests do not cover: `sp-setpiece` stages no `phys_*` body, so its
workload digest is the empty-set value in every run and proves nothing, and
the last checkpoint of `sp-ragdolls` is empty too because the corpses have
burnt away by then. The replay evidence is the first two checkpoints of
`sp-ragdolls` and every checkpoint of `sp-explosion`, `sp-stacks` and
`sp-flatstacks`.

### Defect repros (PHYS-01)

`tools/tests/physics_defects.py`, both modes on the same binary, Air Defense
1.

| Repro | Legacy | Enhanced | Verdict |
| --- | --- | --- | --- |
| Resting jitter: crate and barrel stacks on the sloped crater floor, nudged, read from 12 to 20 seconds | 1,767 sleeps and 1,757 wakes; one body still awake | 22 sleeps and 11 wakes; none awake; no drift | Legacy shows it; enhanced does not |
| Twitching: the eight placed marine ragdolls, nudged on the chest | One still awake after 10 seconds, all at rest after 30 | All at rest after 10 seconds | Not reproduced as endless; enhanced settles sooner |
| Sliding corpses: six fresh corpses on the crater slope, read from 4 to 8 seconds (the game removes them at about 10) | No drift | No drift | Not reproduced |
| Stuck limbs: marines killed with their backs to a wall, then thrown by a Dark Matter splash; 18 corpses over three stagings | No pierced joints, no embedded bodies | None | Not reproduced in legacy; enhanced equal |
| Joint stretch: six resting corpses thrown by a Dark Matter splash, read every second frame | Largest 85 units; more than 8 units apart for 42 frames | Largest 37 units; more than 8 units apart for 22 frames | Legacy shows it; enhanced is smaller and shorter, not gone |
| Tunnelling: a marine helmet fired at the terrain at 600 to 9,000 units/s, straight on to 30 degrees off, 18 launches | None behind the surface | None | Not reproduced; the native test covers the kernel |
| Placed ragdolls: the eight placed marines settling from the map's pose | No pierced joints; none sinks | No pierced joints; the largest sink is 1.0 unit | Not reproduced in legacy; enhanced within the 2-unit tolerance |

All seven pass on Windows; on Linux x64 `placed-ragdolls` fails (see
[Linux x64 and ARM64](#linux-x64-and-arm64-phys-05-phys-06)). How four of
these got there:

- **Placed ragdolls.** Until the third round `env_ragdoll_marine_fullgear_6`,
  which the map lays across the lip of a ledge, sank 7.9 units and came to
  rest with two joints pierced. Its limbs meet the ledge edge on edge, and
  the contact row acted at the nearest vertex of the limb, its end out over
  the drop, instead of at the ledge. With the gap measured on the features
  the collision model reports, the row acts at the ledge's edge and the
  figure stays (see
  [Contact gaps](#contact-gaps-from-the-collision-models-features)).
- **Stuck limbs.** Until 16 November about one corpse in six in this repro
  came to rest in enhanced mode with a shoulder, elbow or knee a few units
  inside the wall or floor (one to four pierced joints over five settings
  tried; legacy none). Position-only recovery was the cause: limit recovery
  was solved with only the overlapping contacts, joint recovery with none,
  and a move of a unit a step is inside every body's speculative reach, so it
  was never swept. Solving the recovery against every contact did not cure
  it, because the surfaces entered were ones the contact query had not
  reported. Sweeping the whole body whenever recovery moved it did, but it
  also stopped the real motion of bodies resting on uneven ground (9 of 17
  rocket-splashed corpses then moved less than 32 units, against 7 before
  and 3 in legacy). Sweeping only the recovery's own share of the motion
  removed the pierced joints in all five settings and left real motion alone.
  The third round found one more path, over 18 corpses instead of six: a
  corpse jittering at about 5 units/s after the splash walked the corner of
  its torso 3.5 units through a thin sheet of scenery by the crashed dropship
  (`func_static_54069`) over some forty steps, each move too small to be swept and off the line
  its contact query had covered. The creep audit (figures, point 9) puts
  such a body back: 0 of 18 corpses with it, 1 of 18 without, the same build
  otherwise.
- **Joint stretch.** With the 32 units/s correction alone (the 14 November
  solver) enhanced corpses stayed visibly stretched longer than legacy's on
  every hit: 21, 45, 89 and 76 frames against legacy's 3, 13, 34 and 46 for a
  falling machinegun hit and rocket, grenade and Dark Matter splashes.
  Position-only recovery, the rising velocity bound and the stopping surface
  in the manifold brought that to 13, 30, 50 and 59 frames against legacy's
  14, 158, 87 and 113 over three stagings. The velocity bound without the
  position-only recovery was far worse (270 to 636 frames): the energy guard
  takes back what the correction adds, and the joint never closes.
- **Tunnelling.** A first version of this repro judged each body where it
  came to rest and found one of 18 behind the surface in legacy, and later
  one in enhanced. The trace showed a ricochet: the 9,000 units/s helmet
  struck the terrain, left at 3,700 units/s and came down a thousand units
  away behind a rise. The repro now reads each body ten frames after its
  launch. Legacy sweeps every body every step and does not tunnel here.

### Contact gaps from the collision model's features

A contact query (`idClip::Contacts`) sweeps a body along its motion and
returns every pair of features that would meet, each with the point where
they would meet, the contact normal, its type and the two features. The
enhanced step needs one more thing the query does not give: how far apart the
pair is now. Until the third round that gap was estimated from the body
vertex nearest the line back along the sweep. Three traces on Air Defense 1
showed the estimate failing on faceted ground:

| Case | Estimated | What happened |
| --- | --- | --- |
| A crate flying at the crater wall at 296 units/s; the sweep reached 5.9 units | The wall was measured 33.5 units away (the far side of the crate) | Nothing held the crate back and it ended 3 units inside the wall |
| A barrel knocked along the concave crater floor; a contact kept from the previous step was measured against every vertex of the barrel | 5.9 units of overlap with a facet the barrel was 0.8 units above | The contact counted as deep overlap, and one step moved the barrel 21 % of its distance |
| A limb of `env_ragdoll_marine_fullgear_6` lying across the lip of a ledge, edge on edge | The contact row acted at the nearest vertex of the limb: its end, out over the drop | The figure sank 7.9 units and rested with two joints pierced |

The collision model already says which features meet, so the gap is now
measured on them (`EnhancedContacts.h`): a body vertex above the polygon's
plane; a body edge above the obstacle's point, with the row at the
obstacle's edge; an obstacle vertex below the body polygon's plane, with the
row at the obstacle's vertex. A kept contact remembers its feature. The
effect is in the tables that follow: the placed ragdoll stays on its ledge,
joints after a Dark Matter splash stay visibly apart for fewer frames than
before, and mean corpse travel after a splash is at legacy's.

### Friction calibration

Legacy's contact friction is not a coefficient. Every step each contact
takes that fraction of the body's sliding velocity (0.05 for a stock
moveable, 0.2 for a crate), so a sliding body slows in proportion to its
speed, a body on a slope creeps at a steady speed instead of stopping, and
with its collisions bouncing it off the floor it spends much of a slide not
touching anything. No single coefficient reproduces that, but one factor
between the authored value and the coefficient can match how far a shoved
body slides, and that is what was fitted: one kind of body on Hangar
Perimeter's flat floor, every body shoved along the floor with the same
impulse, three stagings pooled (six bodies). Median travel in units:

| Body, shove (launch speed) | Legacy | Factor 12 | Factor 6 | Factor 3 | Factor 2 |
| --- | --- | --- | --- | --- | --- |
| Crate, 4,000 (122 units/s) | 13 | 10 | 10 | 13 | |
| Crate, 10,000 (305 units/s) | 51 | 31 | 31 | 51 | |
| Barrel, 10,000 (112 units/s) | 26 | 8 | 16 | 29 | 31 |
| Barrel, 20,000 (224 units/s) | 60 | 32 | 47 | 44 | 50 |
| Barrel, 40,000 (447 units/s) | 70 | 60 | 60 | 63 | 62 |

A crate's coefficient is clamped at 1.2 for both 12 and 6, so those columns
agree for crates. At 12 a barrel stops in a third of legacy's distance at the
lowest speed and a crate in 0.6 to 0.8 of it; at 3 crates match legacy and
barrels are within about a quarter of it; 2 is no better for barrels and was
not measured for crates. The factor is 3: 0.15 for the stock 0.05, 0.6 for a
crate. These columns are from ad hoc traces of this round's work, with the
impact rules already as shipped. The shipped factor is kept honest by two
harness scenes, `physics_envelope.py --scene crates-flat` and `--scene
barrels-flat`, judged like heavy hits (legacy / enhanced):

| Body, shove | Result | Bodies | Median travel | Mean travel | Largest travel |
| --- | --- | --- | --- | --- | --- |
| Crate, 4,000 | PASS | 6 / 6 | 13 / 13 | 19 / 18 | 54 / 36 |
| Crate, 7,000 | PASS | 6 / 6 | 25 / 30 | 32 / 35 | 59 / 55 |
| Crate, 10,000 | PASS | 6 / 6 | 50 / 51 | 52 / 52 | 63 / 105 |
| Barrel, 10,000 | PASS | 6 / 6 | 26 / 29 | 37 / 25 | 64 / 47 |
| Barrel, 20,000 | PASS | 6 / 6 | 60 / 44 | 68 / 54 | 144 / 127 |
| Barrel, 40,000 | PASS | 6 / 6 | 70 / 63 | 87 / 72 | 166 / 138 |

Faster crates reach the room's walls and steps, which then decide where they
stop: at 20,000 legacy's travel 139 units at the median and end 17 units
higher than they started, enhanced's travel 47 and stay on the floor. The
crate scene therefore ends at 10,000.

### Behaviour envelope (PHYS-04)

`tools/tests/physics_envelope.py`. Legacy / enhanced; travel in units from
the report before the hit to the one six seconds later; heavy and falling
classes pooled over three stagings. A heavy hit is judged on the bodies it
reached, those whose velocity it changed: on the scenes below that is every
body, on The Awakening's trench about half.

**Strogg marine corpses on Air Defense 1's crater floor (`--scene ai`): all
classes pass.** Fourth round, both modes hit from one saved start.

| Hit | Class | Result | Corpses | Median travel | Mean travel | Largest travel | Peak speed | Largest stretch, frames visible |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| raw impulse | momentum | PASS | 6 / 6 | momentum change within 2 % in both modes | | | | |
| machinegun | light | PASS | 6 / 6 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 3 for 0 / 1 for 0 |
| machinegun, still falling | light | PASS | 17 / 17 | 60 / 51 | 56 / 64 | 91 / 117 | 125 / 158 | 25 for 14 / 20 for 13 |
| gauntlet | light | PASS | gibs every corpse in both modes | | | | | |
| hyperblaster splash | light | PASS | 6 / 6 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 3 for 0 / 1 for 0 |
| weak vehicle-sized bump | light | PASS | 6 / 6 | 1 / 1 | 1 / 1 | 3 / 2 | 40 / 27 | 3 for 0 / 2 for 0 |
| rocket direct | heavy | PASS | gibs every corpse in both modes | | | | | |
| rocket splash | heavy | PASS | 17 / 17 | 141 / 110 | 129 / 113 | 262 / 250 | 993 / 905 | 37 for 212 / 16 for 16 |
| grenade splash | heavy | PASS | 17 / 17 | 141 / 110 | 129 / 113 | 262 / 250 | 993 / 905 | 37 for 212 / 16 for 16 |
| grenade splash, still falling | heavy | PASS | 17 / 17 | 274 / 280 | 217 / 227 | 461 / 445 | 1,156 / 972 | 37 for 87 / 20 for 14 |
| Dark Matter splash | heavy | PASS | 17 / 17 | 211 / 183 | 231 / 198 | 526 / 525 | 1,737 / 1,631 | 103 for 118 / 67 for 55 |

Every enhanced splash spreads the push of a light limb over its figure, so no
struck body changes speed by more than 300 units/s against legacy's 1,058 to
2,116. On every splash enhanced joints are visibly apart for fewer frames
than legacy's, and by less. The stretch columns of the resting classes start
at the loaded save; those of the falling classes include the kill.

**What the common start changed.** With each mode killing and settling its
own ring (rounds two and three) the rocket and grenade splash read 110 / 71
units at the median on Windows and 144 / 39 on Linux, a **FAIL** there
against the 0.4x bound, and after a Dark Matter splash four of seventeen
enhanced corpses ended further away than any legacy one (up to 836 units
against 472). Both were the staging. Traced corpse by corpse, travel follows
the distance from the blast in either mode:

| Mode | Corpses within 30 units of the blast | Their travel 0.7 s after the hit | Corpses 60 to 100 units away | Their travel |
| --- | --- | --- | --- | --- |
| Legacy | none (the nearest lies 31 away) | 13 | 11 | 101 to 210, and one at 12 |
| Enhanced | 4 (20 to 27 away) | 14 to 53 | 10 | 93 to 365, and two at 20 |

A corpse that close gets the push from above: it leaves at 570 to 650
units/s sideways and 180 to 370 downwards where the others leave at 800
sideways, and is driven into the ground. Enhanced corpses lay closer because
a ragdoll that falls on from the motion it had ends nearer the middle of the
crater, where the player stands. From one saved start the largest travel
after a Dark Matter splash is 526 / 525. Loosening or removing the impulse
guard, the creep audit, the stop contacts and the joint-correction cap had
all been tried against the old result on 16 November; none changed it,
because none of them was the cause.

**Where a thrown corpse loses its speed.** Right after a splash both modes
carry the same momentum: a rocket splash leaves each resting corpse's centre
of mass at about 800 units/s, a Dark Matter splash at about 1,600. Six frames
later legacy still has roughly 60 % of it; enhanced, with impact friction
bounded by the resting load as it was until the second round, had 30 %. A
corpse hit in the air lands as far in either mode, so the loss was in the
launch off the ground. Measured over three stagings, as enhanced mean travel
over legacy's:

| Enhanced variant | Rocket splash | Dark Matter splash | Grenade splash, falling | Machinegun, falling |
| --- | --- | --- | --- | --- |
| Impact friction bounded by the resting load (14 November rule), stopping surface in the manifold, position-only recovery | 0.55 | 0.49 | 0.96 | 1.10 |
| The same with fast-sliding contacts keeping half their friction | 0.55 | 0.58 | 1.04 | 1.13 |
| The same with a quarter | 0.55 | 0.56 | 1.03 | 1.11 |
| Frictionless impacts | 0.50 | 0.71 | 0.99 | 1.12 |
| Frictionless impacts, recovery swept on its own (second round) | 0.65 | 0.71 | 1.02 | 1.16 |
| The same with contact gaps measured on the collision model's features, and the creep audit (third round, shipped) | 0.97 | 1.30 | 1.04 | 1.15 |

Sliding friction does not decide it, so the sliding factor was dropped.
Friction on the landing contacts does for the faster hit: a thrown corpse
touches down on several bodies at once with up to three contacts each, and
one resting load of friction on each is far more than legacy's none. The
second round put the remaining third down to momentum legacy keeps through
its 256 units/s joint correction. That was wrong. It was the contact gaps: a
limb's gap to faceted ground was misjudged, which held limbs back or let them
into the ground, and each one stopped that way pulled on its figure. With the
gaps measured exactly, mean travel after a rocket splash is at legacy's and
the median at 0.64x; after a Dark Matter splash the median is at legacy's
and the mean above it, because of the four corpses that slide on.

**Crates and barrels on the same crater floor (`--scene moveables`): all
classes pass.**

| Hit | Class | Result | Bodies | Median travel | Mean travel | Largest travel | Peak speed |
| --- | --- | --- | --- | --- | --- | --- | --- |
| raw impulse | momentum | PASS | 6 / 6 | momentum change within 2 % in both modes | | | |
| machinegun, gauntlet, hyperblaster splash, rocket direct | light and heavy | PASS | 6 / 6 or 18 / 18 | 0 / 0 (damage does not push these bodies in either mode) | | | |
| weak vehicle-sized bump | light | PASS | 6 / 6 | 38 / 29 | 80 / 69 | 208 / 165 | 128 / 128 |
| rocket splash | heavy | PASS | 18 / 18 | 57 / 32 | 91 / 51 | 222 / 141 | 297 / 296 |
| grenade splash | heavy | PASS | 18 / 18 | 57 / 32 | 91 / 51 | 222 / 141 | 297 / 296 |
| Dark Matter splash | heavy | PASS | 18 / 18 | 133 / 58 | 135 / 64 | 263 / 142 | 341 / 345 |

Until the third round the three splash classes failed at 0.38x, 0.38x and
0.30x of legacy's median. They pass now at 0.57x and 0.44x, with the mean at
0.56x and 0.47x. Three things were wrong, found by tracing one crate and one
barrel step by step in both modes:

- **Friction.** Enhanced crates and barrels left as fast as legacy's and lost
  their speed in a third of the distance: a barrel lost 10 units/s a step
  where legacy's lost 2. The mapping from the authored friction was four
  times too strong (see [Friction calibration](#friction-calibration)).
- **Impacts.** A splash drives a resting body into the slope it stands on.
  Legacy answers with a frictionless rebound and the body skips up the slope
  at nearly its full speed; enhanced took the normal part of the speed with
  no rebound, because the contact was not a new one, and friction on that
  impact took most of the rest.
- **Contact gaps.** On the crater's faceted floor the estimated gaps were
  wrong often enough to stall bodies (see
  [Contact gaps](#contact-gaps-from-the-collision-models-features)).

What is left is the slope. Legacy's contact friction cannot hold a body on a
slope: once knocked loose, a barrel (authored friction 0.05) slides down
anything steeper than about 7 degrees and does not come to rest until the
ground flattens, so on the crater half of legacy's bodies run on to its
floor, 130 to 260 units away. Enhanced bodies have a coefficient of friction
and stop on the slope when they have slowed enough (a barrel on up to about
8 degrees, a crate on up to about 30). Four of 18 travel 130 units or more
after a rocket splash and three after a Dark Matter splash, against seven
and nine in legacy. On a flat floor most of that difference is gone:

**The same crates and barrels on Hangar Perimeter's flat floor (`--scene
moveables-flat`): all classes pass.**

| Hit | Class | Result | Bodies | Median travel | Mean travel | Largest travel | Peak speed |
| --- | --- | --- | --- | --- | --- | --- | --- |
| raw impulse | momentum | PASS | 5 / 5 | momentum change within 2 % in both modes | | | |
| machinegun, gauntlet, hyperblaster splash, rocket direct | light and heavy | PASS | 5 / 5 or 13 / 13 | 0 / 0 | | | |
| weak vehicle-sized bump | light | PASS | 5 / 5 | 15 / 15 | 16 / 12 | 21 / 17 | 128 / 118 |
| rocket splash | heavy | PASS | 13 / 13 | 24 / 19 | 32 / 23 | 147 / 50 | 303 / 287 |
| grenade splash | heavy | PASS | 13 / 13 | 24 / 19 | 32 / 23 | 147 / 50 | 303 / 287 |
| Dark Matter splash | heavy | PASS | 13 / 13 | 38 / 24 | 102 / 84 | 335 / 284 | 596 / 575 |

**The Awakening's grunts on `m01_stranarus_trench2` (`--scene awakening`,
`fs_game q4xbase`): all classes pass.**

| Hit | Class | Result | Corpses | Median travel | Mean travel | Largest travel | Peak speed | Largest stretch, frames visible |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| raw impulse | momentum | PASS | 7 / 7 | momentum change within 2 % in both modes | | | | |
| machinegun | light | PASS | 7 / 7 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 37 for 47 / 10 for 3 |
| machinegun, still falling | light | PASS | 22 / 22 | 39 / 42 | 88 / 94 | 256 / 265 | 168 / 182 | 37 for 54 / 10 for 7 |
| gauntlet | light | PASS | 7 / 7 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 37 for 47 / 10 for 3 |
| hyperblaster splash | light | PASS | 7 / 7 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 37 for 47 / 10 for 3 |
| weak vehicle-sized bump | light | PASS | 7 / 7 | 0 / 0 | 0 / 2 | 1 / 9 | 7 / 27 | 37 for 47 / 10 for 3 |
| rocket direct | heavy | PASS | 22 / 22 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 37 for 54 / 10 for 7 |
| rocket splash | heavy | PASS | 22 / 22 (reached 12 / 11) | 41 / 10; reached 95 / 87 | 78 / 66 | 345 / 297 | 556 / 449 | 37 for 89 / 16 for 12 |
| grenade splash | heavy | PASS | 22 / 22 (reached 12 / 11) | 41 / 19; reached 95 / 87 | 78 / 68 | 345 / 300 | 556 / 449 | 37 for 89 / 10 for 10 |
| grenade splash, still falling | heavy | PASS | 22 / 22 (reached 16 / 16) | 145 / 154; reached 243 / 201 | 188 / 169 | 331 / 328 | 652 / 643 | 37 for 96 / 13 for 23 |
| Dark Matter splash | heavy | PASS | 22 / 22 (reached 12 / 11) | 144 / 44; reached 312 / 344 | 162 / 136 | 489 / 407 | 1,068 / 970 | 37 for 173 / 40 for 62 |

This scene is why a heavy hit is judged on the bodies it reached. The ring
stands on the lip of a trench, and by the time the corpses have settled about
half of them lie down in it, out of the blast's reach: ten of 22 in legacy and
eleven in enhanced do not move at all. The median of all 22 then sits between
the last corpse that stayed and the first that moved, 41 against 10 units
for a rocket splash, although the corpses the blast did reach travelled alike
(95 against 87 at the median, 144 against 131 at the mean). Two earlier
failures on this scene were staging as well. The splash medians of 1.6x and
1.8x in the second round, and a bumped corpse that moved 223 units, came from
one grunt that dies on the trench's edge and is still falling four seconds
after the kill in both modes (22 units/s in legacy, 63 in enhanced): the
scene now waits ten seconds, and keeps the corpses for thirty with the
player's own corpse setting (`g_corpseRemoveDelaySP`), since the game removes
them at ten. Legacy's quiet kill alone stretches a grunt by 37 units for 47
frames; enhanced by 10 for three.

**Player corpses on `mp/q4dm1` (`--scene mp`, a listen server with six
paused bots): ten of eleven classes pass at three repeats; the eleventh
passes at six.**

| Hit | Class | Result | Corpses | Median travel | Mean travel | Largest travel | Peak speed | Largest stretch, frames visible |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| raw impulse | momentum | PASS | 6 / 6 | momentum change within 2 % in both modes | | | | |
| machinegun | light | PASS | 18 / 18 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 86 for 33 / 14 for 10 |
| machinegun, still falling | light | **FAIL** | 18 / 18 | 51 / 59 | 48 / 61 | 66 / 198 | 173 / 132 | 10 for 2 / 31 for 11 |
| the same, six repeats | light | PASS | 36 / 36 | 56 / 59 | 76 / 56 | 503 / 90 | 652 / 243 | 84 for 57 / 10 for 3 |
| gauntlet | light | PASS | 18 / 18 | 0 / 0 | 0 / 0 | 0 / 0 | 7 / 0 | 92 for 62 / 17 for 30 |
| hyperblaster splash | light | PASS | 18 / 18 | 0 / 0 | 0 / 0 | 7 / 0 | 10 / 0 | 16 for 7 / 11 for 3 |
| weak vehicle-sized bump | light | PASS | 18 / 18 | 2 / 1 | 2 / 1 | 6 / 2 | 41 / 28 | 20 for 7 / 11 for 7 |
| rocket direct | heavy | PASS | 12 / 18 (legacy gibs six) | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 21 for 11 / 16 for 13 |
| rocket splash | heavy | PASS | 18 / 18 | 0 / 0 | 0 / 0 | 0 / 8 | 0 / 159 | 19 for 8 / 7 for 0 |
| grenade splash | heavy | PASS | 18 / 18 | 0 / 0 | 0 / 2 | 0 / 33 | 0 / 156 | 34 for 23 / 12 for 4 |
| grenade splash, still falling | heavy | PASS | 18 / 18 | 55 / 57 | 50 / 76 | 65 / 476 | 362 / 287 | 57 for 25 / 11 for 8 |
| the same, six repeats | heavy | PASS | 36 / 36 | 58 / 54 | 77 / 51 | 430 / 76 | 170 / 367 | 13 for 29 / 23 for 8 |
| Dark Matter splash | heavy | PASS | 18 / 18 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 84 for 63 / 13 for 7 |

Every run of this scene draws its own spawn points, and about one corpse in
twenty lies where its collapse carries it 200 to 500 units down the map, in
either mode. At three repeats the still-falling machinegun class failed
because enhanced drew one (198 units) and legacy none (largest 66, bound
114). At six repeats legacy drew two (338 and 503) and enhanced none
(largest 90). Over the second and third rounds' runs of the two falling
classes legacy shows nine corpses beyond 200 units (278 to 503) and enhanced
four (470 to 508), plus that 198. The falling classes of this scene are
therefore pooled over six repeats (`--stagings 6`); three measure the draw.

A player's corpse at rest is not moved by a later hit in legacy. In enhanced
one of 18 moved 8 units after a rocket splash and one 33 after a grenade
splash, at up to 159 units/s; why those two moved was not looked into, and
both are inside the heavy bound. Legacy stretches a player corpse by 10 to 92 units for 2 to 63
frames over these hits; enhanced by 7 to 31 units for at most 30 frames.

**Bodies held by the world and client debris
(`tools/tests/physics_coverage.py`).** The stock maps' hanging corpses
(`env_hanging_body_2`) are `idAFEntity_Base` entities, which never load their
figure: they are static scenery in both modes. The swinging lights are real
figures fixed to the ceiling at one end, and stiff: a 12,000-unit impulse
moves one by half a unit in legacy.

| Case | Result | Legacy | Enhanced |
| --- | --- | --- | --- |
| Air Defense 1's six lights, light impulse | PASS | swing 0.01 | swing 0.05 |
| Air Defense 1's six lights, heavy impulse | PASS | swing 0.56, stretch 0.13 | swing 0.95, stretch 0.14 |
| Air Defense 1's six lights, machinegun, rocket splash | PASS | no motion (damage does not push them) | no motion |
| Putra's 21 lights, light impulse | PASS | swing 0.02 | swing 0.04 |
| Putra's 21 lights, heavy impulse | PASS | swing 0.55, ends 0.53 from where it hung, stretch 0.8, all at rest after 15 seconds | swing 0.97, ends 0.55 away, stretch 1.3, all at rest |
| Putra's 21 lights, machinegun, rocket splash | PASS | no motion | no motion |
| Debris from four launching barrels | PASS | 1 piece, 232 units from the blast, 683 units/s | 1 piece, 241 units, 734 units/s |

Putra's heavy impulse failed until the third round: one light swung 4.3
units and two of the 21 were still awake after 15 seconds. Those two
(`env_swinging_light_16` and `_17`) hang with every body below the top link
inside geometry, the lamp included; two more have three and five links
inside it. Legacy sweeps
every body every step, and a sweep that starts inside a surface cannot move,
so the map's own geometry pins them: that is why a 12,000-unit impulse moves
a light half a unit. Enhanced does not sweep slow bodies, and its overlap
recovery pushed the links out of the beam, after which the chain lay draped
over it and never settled. A body that is inside static geometry when its
figure wakes is now left where the map put it (figures, point 9). Sweeping
those bodies every step as well, without that rule, made it worse (a swing of
31 units and joints 9 units apart), because recovery pushed while the sweep
held.

The debris sample is one piece: stock single-player explosions throw little
client debris, and the multiplayer workload's gibs and brass are measured for
cost only.

### The stock campaign with the option on (PHYS-04)

`tools/tests/physics_campaign.py`.

**Level chain.** One client walks the 31 stages from `game/airdefense1` to
`game/core2`. On each map the map's own bodies run for eight seconds while
`physicsStats` records, `physicsQuality *` reads every rigid body and figure,
the game is saved and that save loaded, and the map's own end-level target
performs the level change. Both modes pass: 62 loads of 62 in order, each in
the mode asked for, the game ends at `core2`, no errors (397 warnings in
legacy, 396 in enhanced; the retail maps' own, as recorded in the
[retail baseline ledger](retail-baseline-ledger.md)). The maps hold 1,675
simulated bodies between them. Over the 31 settle windows legacy spent 1,742
ms on physics and enhanced 355 ms, a fifth. Bodies that left the world: 3 in
legacy, 2 in enhanced. Joints pierced by geometry after settling: 11 in each
mode, on the same maps (Putra's lights, and one each in Hub 1 and Medical
Labs); the two extra that enhanced showed in the second round, Air Defense
1's placed marine, are gone. Four maps cost more in enhanced mode, one run
each: Waste (142 ms against 101, worst frame 5.9 ms against 1.2), Convoy 2
(17 ms against 2, with one frame of 14.9 ms), Convoy 2b (10 against 8) and
Hangar Perimeter (3.5 against 1.4).

The first enhanced walk found two pickups out of place, which legacy leaves
where the map puts them: Network 1's `ammo_railgun_moveable_1` ended 822
units away and Core 2's `ammo_shotgun_14` 740. Enhanced rigid bodies waited
20 quiet steps before resting, where legacy rests a placed body on its first
step; in that window the pickup's support contacts aged out of the manifold
and it tipped off its perch. The rest test for bodies that have not moved
since they were created fixed both (Network 1: no motion in either mode;
Core 2: the same one ammo box settles 14 units against legacy's 17). Two
placed bodies were still out of place after that round: Storage 2's
`ammo_railgun_1` settled 20 units from its spot and Air Defense 2's
`env_ragdoll_marine_fullgear_7` fell 134 units from where legacy leaves it.
Both rest on an edge, and both stay where the map puts them now that edge
contacts are held at the edge (no body on Storage 2, Network 1 or Air
Defense 2 moves more than 8 units in the first six seconds in either mode).

**Set pieces.** Each is driven by the map's own trigger or script function,
in both modes; travel is each driven body's displacement after ten seconds.

| Set piece | Result | Legacy | Enhanced |
| --- | --- | --- | --- |
| Air Defense 1: the blasted grunt (`gruntBlasted`) | PASS | peak 937 units/s; the script freezes the body half a second later | peak 973 units/s; frozen the same way; two impulse clamps |
| Building B: the marine thrown at the wall (`ChuckBody1`, impulse 200,000) | PASS | travels 265 units, at rest; peak 2,492 units/s | travels 207 units, at rest; peak 1,543 units/s; twelve impulse clamps |
| Building B: the falling barrels (`Weld_guard`) | PASS | fall 266 and 216 units, at rest | fall 278 and 269 units, at rest |
| Process 1: the knocked-down grate (`knockdownGrate`, impulse 50,000,000) | PASS | falls 135.8 units, at rest | falls 135.7 units, at rest |
| Storage 2: the falling panel (`panel_fall`) | PASS | already on the floor when shown; no motion | the same |
| Hub 1: the grate (`grate`) | PASS | falls 215 units, travels 243, at rest | falls 200 units, at rest |

Vehicles, projectiles and flying AI on rigid-body physics keep the legacy
step in both modes by design, so the chain exercises them unchanged.

### Visual review

`sp-visual` and `sp-visual-rigid`: 17 moments, three view directions, both
modes, 102 frames read side by side as contact sheets. They are hidden-window
captures at 100 degrees field of view with the weapon hidden and brightness
raised, not a windowed session watched in motion. The sheets were made again
on the third round's build and read the same way as the second round's.

- Marines killed by machinegun and rocket-splash hits, 0.2 to 5 seconds
  after death: in both modes the splashed ones are airborne at half a second
  and all are down by 2.5 seconds, in poses that read as bodies; no limb
  stands visibly stretched or through the ground in any frame; the corpses
  burn away from about five seconds in both.
- The same corpses thrown by a grenade splash, 0.2 to 5 seconds after: flung
  in both modes, gone (burnt away) by five seconds in both.
- The sloped-floor stacks, four seconds after the nudge: legacy's crate tower
  still stands and its barrel stack has come apart; enhanced's crate tower
  has toppled and its barrel stack stands. Two seconds after a rocket splash
  the same holds, with one legacy barrel thrown past the camera. Neither is
  wrong, but which stack survives a knock differs between the modes.

### Saves, clients, demos and presentation (PHYS-05)

`tools/tests/physics_integration.py`, all seven checks pass on the final
build: a save made mid-motion in legacy loads in enhanced, one made in
enhanced loads in legacy and in enhanced, each restoring the bodies where
they were and settling without errors or runaway speeds; a legacy client on an
enhanced listen server and an enhanced client on a legacy one both commit the
server's mode; an MVD recorded on an enhanced server plays to its end in
enhanced mode on a client set to legacy; a legacy client joins an enhanced
dedicated server and follows it. The campaign chain adds a save and a load on
each of the 31 stock maps in each mode.

Interpolation at high refresh
(`tools/tests/presentation_look_runtime.py --scenario ragdoll debris death`
at 240 Hz, `--cvar g_enhancedPhysics 0` and `1`): all three scenarios pass in
both modes on the third round's build, with no gameplay inside a draw. Posed
figure frames were blended between simulation steps 326 times of 337 in
enhanced mode for the falling ragdoll, 189 of 189 with debris flying and 306
of 311 for the death camera (97 to 100 %); legacy 1,882 of 1,963, 1,319 of
1,375 and 1,479 of 1,576 (94 to 96 %). Enhanced poses a fifth as many figure
frames over the same capture because the map's other figures are asleep. Both steps run once per game tic,
so the sample cadence the presentation layer sees is unchanged.

The first integration run on 14 November failed two checks for reasons
outside physics. The dedicated server's 50-second run ended before the client
had finished starting, so the check now keeps it up for 120 seconds. MVD
playback from a fresh client died at the map load with `spawn_entnum 4095 out
of range`: a 9 November bounds check in `RegisterEntity` rejected
`ENTITYNUM_NONE`, the slot of the offline demo viewer, so offline MVD playback
was broken in 0.14.1. The check now admits every slot of the entity table.

### CPU on the reference workloads (PHYS-02, PHYS-03, PHYS-06)

`physics_workloads.py --mode both --frames --rest env_ragdoll`, legacy /
enhanced. Totals are aggregate physics milliseconds over the workload (rigid
bodies plus figures); p99 and worst are per frame. `sp-ragdolls` stages three
of its eight marines and `sp-explosion` 13 of its 16 bodies: `physicsSpawn`
skips placements the static world blocks. The step columns count the steps in
which a rigid body or a figure moved; they are the same in every run of a
mode on one platform.

**Windows x64, five runs each, the map's ragdolls at rest.** All 50 runs
pass and each mode repeats its digests.

| Workload | Total, floor | p99, floor | Worst frame, floor | Total, median | Rigid-body steps | Figure steps | Sleeps, wakes | Enhanced LCP fallbacks |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| sp-ragdolls | 67.9 / 57.6 | 0.64 / 0.54 | 0.69 / 0.68 | 106 / 82 | 0 / 0 | 332 / 340 | 3, 0 / 3, 3 | 15 (0.6 ms) |
| sp-explosion | 65.9 / 68.1 | 0.36 / 0.23 | 1.80 / 2.68 | 97 / 101 | 2,260 / 2,710 | 0 / 0 | 39, 26 / 105, 93 | 0 |
| sp-setpiece | 9.8 / 8.8 | 0.06 / 0.05 | 0.38 / 0.38 | 13 / 18 | 732 / 732 | 1 / 1 | 0, 1 / 0, 1 | 0 |
| sp-stacks | 118.0 / 26.1 | 0.15 / 0.14 | 0.90 / 0.83 | 195 / 35 | 3,434 / 876 | 0 / 0 | 1,675, 1,667 / 20, 11 | 0 |
| sp-flatstacks | 11.7 / 14.5 | 0.06 / 0.10 | 0.81 / 0.81 | 22 / 25 | 164 / 425 | 0 / 0 | 12, 3 / 16, 7 | 0 |

- **Total.** Enhanced costs less on three workloads (15 % on `sp-ragdolls`,
  10 % on `sp-setpiece`, 78 % on `sp-stacks`) and more on two: 3 % on
  `sp-explosion` and 24 % on `sp-flatstacks`, 2.2 and 2.8 ms over the whole
  workload. Both miss the budget; bounds that would cover them are among the
  [proposed exceptions](#budgets-and-tolerances).
- **Totals follow step counts.** A step costs about the same in either mode
  (0.03 ms for a rigid body, 0.2 ms for a figure), so each total is the
  number of steps bodies take before they rest. On the sloped stacks legacy's
  bodies wake each other 1,667 times and take 3,434 steps where enhanced's
  take 876. In the explosion scene enhanced's take a fifth more steps than
  legacy's, because each body is held awake for a ten-step quiet window
  before it may rest and neighbours wake it 93 times. On the flat stacks,
  which only have to stand, legacy rests each placed body on its first quiet
  step and enhanced does not.
- **What the third round cost.** After the second round the explosion scene
  measured 46 ms against 264. Most of that gap was the map's placed ragdolls
  settling inside the measurement (see below), and part was crates and
  barrels that stopped at a third of legacy's travel. With their travel
  restored they take as many steps as legacy's, and the rigid-body saving
  that is left is the sleep and wake churn on slopes.
- **Worst frame.** Single runs spread from 0.4 to 113 ms for identical work,
  which is why the floor is used. On the floor enhanced is lower or equal on
  four workloads and 0.9 ms higher on `sp-explosion`, in the frame in which
  the 13 staged bodies take their first step (five legacy readings of that
  frame lie between 1.8 and 4.6 ms, five enhanced between 2.7 and 7.5):
  proposed exception 3.
- **Fallbacks.** The exact LCP solved 15 steps of `sp-ragdolls` instead of
  the iterative solve, for 0.6 ms, and none elsewhere.

**Windows x64, three runs each, as the workloads ran before** (the map's
nine placed ragdolls settling inside the measurement). All 30 runs pass.

| Workload | Total, floor | p99, floor | Worst frame, floor | Total, median | Rigid-body steps | Figure steps | Sleeps, wakes | Enhanced LCP fallbacks |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| sp-ragdolls | 291.1 / 84.3 | 1.13 / 0.70 | 1.24 / 0.89 | 328 / 117 | 0 / 0 | 1,264 / 414 | 5, 0 / 5, 3 | 32 (1.9 ms) |
| sp-explosion | 351.9 / 79.8 | 1.31 / 0.33 | 2.94 / 3.22 | 543 / 116 | 2,322 / 2,706 | 901 / 49 | 42, 27 / 107, 93 | 8 (0.7 ms) |
| sp-setpiece | 232.7 / 22.3 | 0.65 / 0.44 | 0.77 / 0.71 | 292 / 30 | 738 / 738 | 902 / 50 | 2, 1 / 2, 1 | 8 (1.0 ms) |
| sp-stacks | 362.9 / 37.4 | 0.81 / 0.29 | 1.83 / 1.20 | 479 / 45 | 3,434 / 876 | 901 / 49 | 1,677, 1,667 / 22, 11 | 8 (0.7 ms) |
| sp-flatstacks | 18.4 / 15.1 | 0.06 / 0.10 | 6.81 / 1.22 | 30 / 24 | 164 / 425 | 0 / 0 | 12, 3 / 16, 7 | 0 |

Run this way enhanced costs 71 to 90 % less than legacy on the four Air
Defense 1 workloads on Windows, and nearly all of that is the figure-steps
column: the map's placed ragdolls move in about 900 steps of a legacy
workload and 49 of an enhanced one. On Linux the same column reads the other
way round (below). That is why the budget is judged with those ragdolls at
rest. `sp-flatstacks` runs on Hangar Perimeter, which has none, so its two
tables measure the same thing: three runs put legacy's floor at 18.4 ms and
five at 11.7, which is the size of the noise on a workload this small. Its
step counts (164 against 425) are exact, and they say enhanced does more
work there.

Legacy's digests in these runs are the ones recorded on 14 November on all
five workloads. Each mode repeats its own digests, with one exception: one of
the three enhanced `sp-explosion` runs started two game frames earlier than
the other two by the game clock, for a reason not looked into, and its digests differ
from the second checkpoint on, with the same step, sleep and wake counts. The
other two match each other, and all five rested runs match.

`mp-frags` on Windows, three runs (bots are not deterministic, so medians and
ranges):

| | Legacy | Enhanced |
| --- | --- | --- |
| Total ms, median (runs) | 629 (629, 755, 577) | 658 (935, 658, 559) |
| p99 ms, median | 1.27 | 1.15 |
| Worst frame ms, median (range) | 5.67 (5.4-5.9) | 6.11 (6.0-7.2) |
| Rigid-body steps (runs) | 15,120; 24,966; 6,768 | 24,229; 12,754; 12,718 |
| Steps that moved a figure (runs) | 1,880; 1,994; 2,178 | 2,766; 2,286; 1,857 |
| Sleeps, wakes (median run) | 9,511, 9,638 | 1,101, 1,326 |
| Largest joint stretch, frames visible (per run) | 90 for 118; 54 for 113; 28 for 46 | 14 for 26; 23 for 38; 24 for 17 |
| LCP fallbacks | | 153 to 220 (11.7 to 13.0 ms) |

By the letter of the budget (medians of three runs) enhanced is 5 % and
0.4 ms above legacy here. The runs differ from each other by far more than
that (one run's bots throw three and a half times the debris of another's),
so three runs decide nothing in either direction, and the budget is not
shown to be met on this workload. What every run does show is the sleep and
wake churn and the stretched player corpses of legacy, and neither in
enhanced.

### Linux x64 and ARM64 (PHYS-05, PHYS-06)

**Fourth round: Linux x64 on exact math (17 November).** The same sources
as Windows, built after `_FAST_MATH` was removed (see [the Linux build was
running on approximate
math](#the-linux-build-was-running-on-approximate-math-fourth-round)). The
round was wrapped up before the full pass could be repeated, so this is what
ran: the rested CPU workloads and the three checks that had failed. Everything
below this block is the third round's pass on the build with approximate math
and is kept as the record of what that build did.

| Workload | Total, floor | p99, floor | Worst frame, floor | Total, median | Rigid-body steps | Figure steps | Sleeps, wakes | Enhanced LCP fallbacks |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| sp-ragdolls | 82.8 / 136.9 | 0.81 / 0.96 | 0.97 / 1.17 | 102 / 197 | 0 / 0 | 336 / 534 | 3, 0 / 5, 5 | 18 (1.0 ms) |
| sp-explosion | 65.6 / 65.8 | 0.37 / 0.21 | 0.55 / 0.26 | 80 / 85 | 2,198 / 2,709 | 0 / 0 | 36, 23 / 105, 93 | 0 |
| sp-setpiece | 11.2 / 9.9 | 0.07 / 0.06 | 0.35 / 0.44 | 15 / 15 | 732 / 732 | 1 / 1 | 0, 1 / 0, 1 | 0 |
| sp-stacks | 131.9 / 32.9 | 0.18 / 0.17 | 0.24 / 0.21 | 185 / 45 | 3,434 / 876 | 0 / 0 | 1,675, 1,667 / 20, 11 | 0 |
| sp-flatstacks | 11.8 / 15.9 | 0.07 / 0.12 | 0.13 / 0.18 | 19 / 24 | 164 / 425 | 0 / 0 | 12, 3 / 16, 7 | 0 |

Three runs each with the map's ragdolls at rest, legacy / enhanced, while
three Windows clients ran beside them; all 30 pass and each mode repeats its
digests.

- **The two platforms now agree.** `sp-stacks`, `sp-flatstacks` and
  `sp-setpiece` produce the same state digests on Linux as on Windows in both
  modes, with the same step, sleep and wake counts (3,434 / 876 and 164 / 425
  moving steps, 1,675 sleeps and 1,667 wakes against 20 and 11). Legacy's
  ragdoll workload takes 336 moving steps here and 332 on Windows, and its
  explosion scene 2,198 against 2,260 (3,007 on the approximate build).
  `sp-ragdolls` and
  `sp-explosion` still differ in digest, because they spawn their bodies in
  a ring and the ring's sines and cosines come from glibc here and the UCRT
  there.
- **The CPU budget is still missed on two workloads.** `sp-ragdolls` costs
  65 % more in enhanced mode (54 ms over the workload, worst frame 0.20 ms
  higher): the three staged corpses take 534 moving steps to settle against
  legacy's 336, where on Windows they take 340 against 332. That gap is
  larger than on the approximate build (21 %) and is not explained.
  `sp-flatstacks` costs 34 % more (4.1 ms, worst frame 0.04 ms higher), for
  the same 425 steps against 164 as on Windows. `sp-setpiece`'s worst frame
  is 0.09 ms higher. `sp-explosion` costs the same and `sp-stacks` 75 % less.

The three checks that failed on the approximate build, on this one:

| Check | Result | Legacy / enhanced |
| --- | --- | --- |
| `physics_defects.py --repros placed-ragdolls` | PASS | Largest sink 0.0 / 1.0 unit, no joint pierced: the ledge marine stays on its ledge in both modes, as on Windows (0.0 / 1.0). Before: 14.7 / 25.9, with the marine on the floor. |
| the same, `joint-stretch` and `resting-jitter` | PASS | Joints 58 units apart for 28 frames / 33 for 12. 1,768 sleeps and 1,757 wakes / 22 and 11, Windows's counts to within one. |
| `physics_envelope.py --hits rocket-splash,dmg-splash` (one saved start) | PASS | Rocket splash median 150 / 125 units (0.83x), mean 126 / 121, largest 225 / 276. Dark Matter splash 222 / 293 (1.32x), largest 562 / 539. Before, with each mode's own start: 144 / 39. |
| `physics_coverage.py`, Putra's lights, heavy impulse | PASS | Joint stretch 0.81 / 1.32 units, swing 0.55 / 0.97: Windows's numbers to the digit. Before: 1.10 / 2.18. |

Not repeated on the exact build: the unrested workloads, `mp-frags` (24 %
above legacy on the approximate build), the dedicated server, the
integration checks, the other four defect repros, the rest of the envelope
(light and falling classes, crates and barrels, The Awakening, multiplayer),
the rest of coverage, and the ARM64 dedicated server.

**Third round: Linux x64 on approximate math (16 November).**

WSL2, Ubuntu 24.04.3, GCC 13.3, debugoptimized, built from an
export of the third round's snapshot (`c45be1f81`); the client runs under
Xvfb on Mesa's software renderer. Three runs each with the map's ragdolls at
rest, legacy / enhanced; all 30 runs pass without errors and each mode
repeats its digests.

| Workload | Total, floor | p99, floor | Worst frame, floor | Total, median | Rigid-body steps | Figure steps | Sleeps, wakes | Enhanced LCP fallbacks |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| sp-ragdolls | 79.3 / 95.9 | 0.76 / 0.70 | 1.00 / 0.97 | 102 / 113 | 0 / 0 | 339 / 461 | 3, 0 / 3, 3 | 18 (0.7 ms) |
| sp-explosion | 91.9 / 68.1 | 0.34 / 0.22 | 0.37 / 0.26 | 109 / 82 | 3,007 / 2,708 | 0 / 0 | 461, 448 / 105, 93 | 0 |
| sp-setpiece | 11.0 / 9.0 | 0.06 / 0.05 | 0.37 / 0.41 | 14 / 12 | 732 / 732 | 1 / 1 | 0, 1 / 0, 1 | 0 |
| sp-stacks | 124.8 / 27.4 | 0.15 / 0.15 | 0.19 / 0.17 | 153 / 34 | 3,434 / 872 | 0 / 0 | 2,195, 2,187 / 17, 8 | 0 |
| sp-flatstacks | 10.1 / 18.3 | 0.06 / 0.10 | 0.12 / 0.14 | 13 / 24 | 145 / 538 | 0 / 0 | 12, 3 / 19, 10 | 0 |

Enhanced costs less on three workloads (26 % on `sp-explosion`, 18 % on
`sp-setpiece`, 78 % on `sp-stacks`) and more on two: 21 % on `sp-ragdolls`
(16.6 ms) and 80 % on `sp-flatstacks` (8.2 ms on a 10 ms total). Its worst
frame is lower on three and higher by 0.04 ms on `sp-setpiece` and 0.02 ms on
`sp-flatstacks`. All four miss the budget and are covered only by the
[proposed exceptions](#budgets-and-tolerances). The step columns explain the totals
here as on Windows: the three staged corpses take 461 moving steps to settle
in enhanced mode with GCC against legacy's 339, and 340 against 332 with
MSVC. Enhanced's rigid-body step counts are the same on both compilers to
within one per cent (2,708 and 2,710 in the explosion scene, 872 and 876 on
the sloped stacks) where legacy's differ by a third in the explosion scene
(3,007 and 2,260): which platform shows an overrun on that scene is legacy's
variation, not enhanced's.

**As the workloads ran before, one run each** (the map's placed ragdolls
settling inside the measurement; run beside another client, so the step
counts are exact and the milliseconds are not a floor):

| Workload | Total | Rigid-body steps | Figure steps | Sleeps, wakes | Enhanced LCP fallbacks |
| --- | --- | --- | --- | --- | --- |
| sp-ragdolls | 122 / 202 | 0 / 0 | 504 / 842 | 7, 0 / 9, 3 | 64 (3.1 ms) |
| sp-explosion | 102 / 191 | 2,210 / 2,710 | 131 / 413 | 39, 22 / 112, 94 | 47 (2.7 ms) |
| sp-setpiece | 51 / 122 | 792 / 738 | 132 / 414 | 6, 2 / 6, 1 | 47 (2.9 ms) |
| sp-stacks | 163 / 154 | 3,434 / 872 | 131 / 413 | 2,199, 2,187 / 23, 8 | 47 (3.5 ms) |
| sp-flatstacks | 10 / 18 | 145 / 538 | 0 / 0 | 12, 3 / 19, 10 | 0 |

Here the map's placed ragdolls move in 131 steps of a legacy workload and
413 of an enhanced one, where on Windows they move in 901 and 49. Unrested,
enhanced therefore costs up to 2.4 times legacy on the Air Defense 1
workloads on Linux and a tenth to a third of legacy on Windows, for the same
staged bodies. Neither says anything about the staged bodies, which is why
the budget rests the map's ragdolls first; but it is what a player on that
map gets, and on Linux enhanced takes three times as many steps to settle
the nine placed ragdolls as legacy does.

Bot rounds on Linux x64, three runs each (medians; bots are not
deterministic):

| | Legacy | Enhanced |
| --- | --- | --- |
| `mp-frags` (listen server) total ms, median (runs) | 1,424 (1,424, 1,277, 1,984) | 1,761 (1,761, 1,703, 2,005) |
| `mp-frags` p99, worst frame median (range) | 0.49, 1.01 (0.8-2.2) | 0.56, 1.30 (1.2-2.3) |
| `mp-frags` steps that moved a figure, rigid-body steps (runs) | 7,029 to 9,026; 42,012 to 96,723 | 8,066 to 8,460; 65,398 to 87,452 |
| `mp-frags` sleeps, wakes (median run) | 38,243, 38,546 | 5,275, 5,810 |
| `mp-frags` largest stretch, frames visible | 78-101 for 349-577 | 20-27 for 103-147 |
| `mp-frags` LCP fallbacks | | 704 to 836 (30 to 39 ms) |
| `ded-frags` (dedicated server) total ms, median (runs) | 757 (882, 757, 701) | 671 (670, 782, 671) |
| `ded-frags` p99, worst frame median (range) | 0.88, 1.30 (1.1-10.6) | 0.94, 8.54 (3.1-8.7) |
| `ded-frags` largest stretch, frames visible | 50-74 for 163-237 | 19-32 for 31-62 |

On the listen-server round enhanced is above legacy on the medians of three
runs, by 24 % in total and 0.3 ms in the worst frame, and its three totals
rank above legacy's three one for one: the budget is missed on `mp-frags` on
Linux. Of enhanced's time 30 to 39 ms a run is the exact solver taking over
700 to 840 steps; the rest of the difference is not explained. Legacy's sleep and
wake churn (38,000 against 5,300) and its stretched player corpses show here
as on Windows.

The dedicated server runs the round in both modes with no renderer and no
errors, at about the same total (2,700 to 4,200 figure steps a run in legacy,
3,100 to 3,400 in enhanced; 190 to 210 LCP fallbacks for 10 to 12 ms). Its
worst frame is one reading per run on a machine that was running two other
clients at the time, and legacy shows a 10.6 ms frame too: three runs do not
decide it either way.

**Other Linux x64 checks.** `physics_integration.py` passes all seven checks
on the final build: saves made in either mode load in the other and in their
own, clients follow a listen server's mode both ways, an MVD recorded on an
enhanced server plays to its end, and a legacy client joins an enhanced
dedicated server. (The test tree is an export; its history is padded to the
real commit count, because a build stamps its saves with that count and the
save validator rejects a build number below 640.)

**The qualification harnesses on a Linux client** (final build, one pass):

| Harness | Result | Legacy / enhanced |
| --- | --- | --- |
| `physics_defects.py` | **6 of 7 pass** | Resting jitter 2,319 sleeps and 2,307 wakes / 23 and 8. Joint stretch 69 units for 28 frames / 21 for 8. No joint pierced in 18 corpses killed against a wall in either mode; nothing beyond a wall in 18 launches. `placed-ragdolls` **fails** (below). |
| `physics_envelope.py --scene ai` | **9 of 11 classes pass** | Rocket and grenade splash on resting corpses **fail**: median 144 / 39 units (0.27x; bound 0.4x), mean 138 / 90. Machinegun into falling corpses 64 / 55, largest 106 / 113. Grenade splash into falling corpses 279 / 274. Dark Matter splash 205 / 151, joints apart for 198 frames at up to 72 units / 54 frames at up to 34. |
| `physics_envelope.py --scene moveables` | 9 of 9 pass | Rocket and grenade splash median 56 / 32 units, Dark Matter splash 129 / 58, a weak bump 38 / 29 (largest 208 / 209). |
| `physics_envelope.py --scene moveables-flat` | 9 of 9 pass | Rocket and grenade splash median 24 / 19 units, Dark Matter splash 42 / 24, a weak bump 15 / 15. |
| `physics_coverage.py` | **8 of 9 pass** | Air Defense 1's six lights swing 0.57 / 0.94 units after a heavy impulse and rest. Putra's 21 lights after a heavy impulse **fail** on joint stretch by 0.09 units: 2.18 against legacy's 1.10, bound 2.10 (swing 0.55 / 0.96, both end 0.54 from where they hung, all at rest; on Windows 1.3 against 0.8, which passes). Debris 213 / 228 units from the blast. |

`placed-ragdolls` on Linux: Air Defense 1's `env_ragdoll_marine_fullgear_6`,
the marine lying across a ledge edge, ends 25.9 units lower in enhanced mode,
at rest on the floor beside the ledge with no joint pierced. Legacy keeps it
on the ledge (it shifts 9 units along it), and its own largest drop over the
same ten seconds is another marine's 14.7 units. On Windows legacy drops none
and enhanced 1.0. The first sample is taken while the map's ragdolls are
still settling, at up to 390 units/s in legacy and 121 in enhanced.

Not run on Linux on the final build: the Awakening and multiplayer envelope
scenes, the flat-floor shove scenes and the campaign harness (chain and set
pieces).

**ARM64.** The AArch64 cross build (GCC, the CI options) of the dedicated
server and multiplayer game module runs `ded-frags` in both modes under
`qemu-aarch64` 8.2.2 user-mode emulation, without errors: 1,255 LCP fallbacks
in enhanced mode, largest stretch 77 units for 1,152 frames in legacy and 50
for 486 in enhanced. Emulated timings are not hardware performance and are
not recorded. The native solver test passes on six builds (x64 with g++ and
clang++, AArch64 with clang++ with and without floating-point contraction,
AArch64 with g++), printing the same values on all of them. No ARM64 client
was run, and nothing ran on ARM64 hardware.

### The Linux build was running on approximate math (fourth round)

The third round ended with the same build passing its gates on Windows and
failing three checks on Linux, and with step counts that "differed by
compiler in both modes". The fourth round found why. It was neither the
enhanced solver nor rounding chaos: the Linux and macOS ports of idlib
compiled different math from Windows.

**Symptom.** On the Linux build the stock maps' placed ragdolls blew apart
when a level started, in legacy mode as well. Sampled on every frame from the
moment Air Defense 1's intro ends, legacy's eight placed marines reached 835
units/s with joints up to 21 units apart on Linux and 14 to 27 units/s with
joints under one unit apart on Windows. Gravity nudged to four values between
1,064 and 1,068 left every Windows ragdoll where it lay; nudged to 1,065,
1,067 and 1,070 it left Linux violent every time (up to 1,664 units/s, joints
11 to 28 units apart), and the ledge marine fell off its ledge in legacy mode
too in two of the three. A difference that survives every
perturbation is a defect, not chaos. On Air Defense 2, which has no intro, a
Linux legacy ragdoll's fastest body went from 13 to 138 units/s in its first
thirteen frames; the Windows one stayed between 1 and 16.

**Cause, traced step by step** with a temporary per-phase trace of the legacy
figure step built on both platforms:

1. On the first step both builds agree to the printed digit through contact
   setup and the primary (joint) solve. In the auxiliary constraints Linux has
   26 rows where Windows has 25, and applies accelerations of up to 9,250
   where Windows applies none.
2. The extra row is the right knee's cone limit, with an error term of -201
   where the other limits have -0.3 to -0.6: Linux judges the knee about 20
   degrees outside its limit in the pose the map authored.
3. The bodies' orientations are identical. The limit's axis, stored in the
   thigh body's frame, is (-0.574, 0, 0.819) on Windows and (0, -0.574, 0.819)
   on Linux: rotated a quarter turn about the thigh.
4. The thigh is a bone-shaped body between the hip and knee joints.
   `idAF::LoadBody` builds its frame with `idVec3::NormalVectors`, which
   takes a fallback frame only when the bone's direction has x and y exactly
   zero. On Windows the right hip and knee have identical x, so the direction
   is exactly (0, 0, -1). On Linux the knee's x is 5.4e-7 off, the direction
   is (3.0e-8, 0, -1) and the frame comes out a quarter turn away. Every
   ragdoll pose stored in a map assumes the Windows frame.
5. The joints come from an animation frame whose quaternion w is
   `idMath::Sqrt( 1 - x*x - y*y - z*z )`. For the right knee Windows gets
   0.994754314 and Linux 0.994754255: one unit in the last place.
6. `src/idlib/math/Math.h` defined `_FAST_MATH` under
   `#if defined( __linux__ ) || defined( MACOS_X )`. With it `idMath::Sqrt`
   is `x * InvSqrt( x )` from a lookup table and two Newton steps, and
   `Sin16`, `Cos16`, `Tan16` and `ATan16` are polynomials; without it they
   are `sqrtf` and the other exact library calls, as on Windows and in the
   retail game. Replaying both forms in float32 reproduces the two values
   above for every joint checked.

**Fix.** `_FAST_MATH` is no longer defined on any platform, and
`tools/tests/physics_enhanced_solver.py`, which CI runs on every platform,
fails if a define of it comes back. On the rebuilt Linux tree the right thigh
bone is exactly vertical again, Air Defense 2's ragdoll reads 12.9 units/s
with joints 0.50 apart on its first sampled frame (Windows 13.1 and 0.51), and
Air Defense 1's read 16.4 and 0.90 (Windows 13.6 and 0.93).

**What this changes beyond physics.** Every user of `idMath::Sqrt`,
`InvSqrt` and the 16-bit trigonometry on Linux, macOS and Android now gets
the Windows results: animation, collision, AAS and rendering math included.
The rigid-body stack workloads (`sp-stacks` and `sp-flatstacks`) now produce the
same state digests on Linux as on Windows, in both modes; workloads that
spawn a ring of bodies still differ, because the ring's sines and cosines
come from two different C libraries. macOS and Android took the same switch and have not
been run since the change.

**Two findings about the evidence itself came out of the same hunt.**

- *Corpses were not hit from the same place in both modes.* The third
  round's failing class, a rocket or grenade splash on resting corpses, was
  the staging: enhanced corpses collapse closer to the player, so four of
  seventeen lay 20 to 27 units from the blast where legacy's nearest lay 31
  away, and in both modes a corpse that close is pushed into the ground and
  travels 13 to 53 units where the others travel 100 to 200. The envelope now stages each scene once, in legacy
  mode, saves it, and hits that save in both modes (see
  [Budgets](#budgets-and-tolerances)). `--own-start` runs the old way.
- *This machine's "retail" maps were not all retail.* The Steam folder used
  for the Windows runs holds loose, locally recompiled map, proc and
  collision files for Air Defense 1 and every multiplayer map, dated March
  2026, which override the packs (38,107 collision polygons for Air Defense
  1 against the pack's 46,640). Linux always ran on a copy of the packs
  alone. From the fourth round the Windows runs use a packs-only view of the
  same install (`--retail`), as Linux does. The placed ragdolls' first
  frames on Air Defense 1 are identical to the digit either way.

### Known defects and what was tried

**Fixed in the third round** (each was open after the second, and is why an
item was not closed then):

| Defect | Cause | Now |
| --- | --- | --- |
| Blasted crates and barrels stopped at 0.3x to 0.4x of legacy's travel | Friction four times too strong; impacts kept friction and did not rebound off a kept contact; estimated contact gaps | 0.44x to 0.57x on the crater, 0.63x to 0.76x on a flat floor; every class passes |
| A figure lying across a ledge edge sank (Air Defense 1's placed marine by 7.9 units with two joints pierced; Air Defense 2's fell 134 units) | An edge-on-edge contact acted at the limb's end, not at the ledge | On Windows sinks 1.0 unit, no joint pierced, and Air Defense 2's stays; on Linux Air Defense 1's slides off its ledge instead (open, 3) |
| The Awakening: splash medians 1.6x and 1.8x of legacy, one bumped corpse moved 223 units | A grunt still falling into the trench when the hit was staged; and a median over bodies half of which the blast never reached | All eleven classes pass; the bodies a splash reaches travel 0.83x to 1.10x as far |
| Putra's lights: one swung 4.3 units, two of 21 never slept | Overlap recovery pushed chain links out of the beam the map hangs them in | Swing 0.97 units, all asleep |
| Storage 2's `ammo_railgun_1` settled 20 units off its spot | The same edge contact as the ledge | Stays put |
| Corpses launched off the ground travelled two thirds as far as legacy's | Estimated contact gaps, not the joint correction it was put down to | Mean 0.97x after a rocket splash, 1.30x after a Dark Matter splash on Windows; the median of resting corpses is still open (2) |

**Fourth round (17 November).** Of the open items below, 2, 3 and 9 were the
Linux build's approximate math and 4 was the envelope's staging; all four are
closed, with the evidence in [the Linux build was running on approximate
math](#the-linux-build-was-running-on-approximate-math-fourth-round) and the
[Linux section](#linux-x64-and-arm64-phys-05-phys-06). Item 1 stands, with
Linux figures of 65 % on `sp-ragdolls` and 34 % on `sp-flatstacks`. New and
open after the fourth round:

- **Enhanced corpses settle differently on the two platforms.** The three
  staged corpses of `sp-ragdolls` take 534 moving steps on Linux and 340 on
  Windows in enhanced mode, where legacy takes 336 and 332. Not explained.
- **The full Linux pass has not been repeated on the exact build.** The round
  was wrapped up at the user's request after the rested CPU workloads and the
  three failed checks.
- **The Windows CPU tables are the third round's build.** The sources have
  moved (about forty commits by other work) and the Windows runs now use the
  retail packs alone; the tables were not re-measured. Legacy's digests on
  current sources still match 14 November's.
- **The campaign walk's timings of 17 November are not usable.** The walk
  passes in both modes on the retail packs (62 of 62 loads, 1,675 bodies, 11
  pierced joints in each mode), but it ran on a machine also running two more
  of these queues, a Linux pass and another session's clients. Waste read
  1,654 ms in enhanced mode against 93 with an 83 ms frame, for 11,896
  rigid-body steps against legacy's 12,757, and legacy shows single
  evaluations of 27 to 54 ms in the same walk. The third round's 142 ms
  against 101 stands until it is measured again on a quiet machine.
- **macOS and Android** share the math change and have not been run.
- **The cross-mode save checks** (`physics_integration.py`) were not repeated
  on current sources; the common-start envelope loads a legacy save in both
  modes on every run and passes.

**Open after the third round** (see the note above for what the fourth
closed).

1. **Processor time on two workloads per platform.** With the map's own
   ragdolls at rest enhanced costs more than legacy on `sp-explosion` (3 %)
   and `sp-flatstacks` (24 %) on Windows and on `sp-ragdolls` (21 %) and
   `sp-flatstacks` (80 %) on Linux: 2.2 to 16.6 ms over a workload. A step
   costs the same in both modes; enhanced bodies take more steps to rest
   there. Bounds that would cover them are written down as [proposed
   exceptions](#budgets-and-tolerances); they are not accepted, and not
   fixes. What was tried against them is in the list below (the sleep
   window, the rest tests, the wake rules): each cut steps on one scene and
   broke another. Without resting them first, Air Defense 1's nine placed
   ragdolls take 413 moving steps to settle in enhanced mode on Linux
   against legacy's 131 (49 against 901 on Windows), so on Linux the
   unrested Air Defense 1 workloads cost up to 2.4 times legacy. The
   multiplayer bot round costs 24 % more than legacy on Linux on the medians
   of three runs (5 % on Windows, inside the spread of its runs); no
   exception is proposed for it.
2. **Resting corpses under a rocket or grenade splash.** Enhanced leaves 8
   or 9 of 17 within 65 units of where they lay and legacy 4, so the class
   median sits in the gap between two groups: 71 units against 110 on Windows
   (passes) and 39 against 144 on Linux (0.27x against a 0.4x bound, fails).
   The cause is not found; what was switched is in the
   [envelope section](#behaviour-envelope-phys-04).
3. **The placed marine slides off its ledge on Linux.** Air Defense 1's
   `env_ragdoll_marine_fullgear_6` ends 25.9 units lower in enhanced mode
   there, at rest on the floor below its ledge with no joint pierced. Legacy
   keeps it on the ledge on Linux (it shifts 9 units along it), and both
   modes keep it there on Windows, where enhanced sinks 1.0 unit. The map's
   placed ragdolls settle differently by compiler in both modes (legacy's
   `_3` drops 14.7 units on Linux and none on Windows), but the repro
   compares the modes on one platform and this fails it.
4. **Corpses that slide on.** After a Dark Matter splash on the crater four of
   17 corpses end further away than any legacy corpse (501 to 836 units
   against 472), and one after a rocket splash (441 against 233). They leave
   no faster than legacy's and stay in the map. The heavy classes bound the
   median, not the largest travel, so this passes its gate.
5. **Crates and barrels on a slope.** A body knocked loose on the crater
   travels about half as far as legacy's at the median, because legacy's
   bodies cannot stop on a slope and enhanced's can. Inside the gate, and
   visible.
6. **Four maps cost more in the campaign walk** (one run each): Waste 142 ms
   against 101, Convoy 2 17 against 2 with one 14.9 ms frame, Convoy 2b 10
   against 8, Hangar Perimeter 3.5 against 1.4. The other 27 cost less or the
   same, and the walk as a whole a fifth of legacy's.
7. **Which stack survives a knock differs** between the modes on the sloped
   floor (see Visual review). Not a defect by any gate, but a visible
   difference.
8. **The multiplayer envelope needs an idle machine.** In multiplayer the
   game clock runs on wall time while the harness waits in frames; on a
   loaded machine the corpses expire before the hit in both modes and the
   classes report no corpses to compare. Its falling classes also need six
   repeats before they measure the modes and not the draw of spawn points.
   Both are limits of the harness, not of either mode.
9. **Putra's lights on Linux.** After a heavy impulse the largest joint
   stretch among the 21 lights is 2.18 units in enhanced mode against
   legacy's 1.10, 0.09 over the coverage bound (a quarter more, or one unit
   more). It is far below what shows (8 units), every light comes to rest,
   and on Windows the same case passes at 1.3 against 0.8.

**Measured and not kept in the third round.** Each of these was built, run
and reverted; the numbers are why.

- *Solving small figures exactly* (the legacy solver for systems under 48
  rows): Putra's lights swung 13.7 units instead of 8. The fault there was
  the recovery, not the iterations.
- *More iterations* (40 and 100 instead of 12): the staged corpses took 372
  and 368 moving steps to settle instead of 414.
- *Sweeping every move off the line a contact query covered*: 6,147 sweeps
  against 1,575 on the ragdoll workload and 580 ms against 103, and limbs
  stopped in flight.
- *Auditing with a long chord* (any body, every 8, 16 or 30 steps): no limb
  left in geometry, but joints stayed visibly apart for 40 to 46 frames, at up
  to 79 units, after a Dark Matter splash, as long as legacy's 42, because the
  straight chord between two poses of a tumbling limb cuts through bumps the
  limb went round. The shipped audit is limited to a chord of two units.
- *Sweeping bodies that start inside geometry every step*, without stopping
  their overlap recovery: Putra's lights swung 31 units.
- *Recovering a contact that has dipped behind its surface to just in front
  of it*: resting corpses drifted 12 to 16 units in the sliding-corpse repro,
  because position-only motion is not resisted by friction.
- *A lower friction for contacts that are already sliding*: the friction
  factor does not move mean travel on the crater once impacts slide (51 to
  53 units at factors 12, 6 and 3), so it was dropped before it was built
  into a default.
- *Rebounding off a kept contact between two moving bodies, or under a
  stack*: the flat-floor stacks never settled (325 ms of physics against
  legacy's 88).
- *Letting a rigid body woken by a neighbour sleep again after two quiet
  steps*: 432 sleep and wake cycles in the explosion scene and 40 % more body
  steps.
- *Not letting a rigid body sleep while it has crept a tenth of a unit*: 46 %
  more body steps in the explosion scene.
- *Resting a quiet rigid body by legacy's test after a few quiet steps*: 236
  to 448 sleep and wake cycles in the explosion scene.
- *A 10-step sleep window at 3 units/s*: fewer steps than the shipped 1.5
  units/s on the flat stacks, but bodies creeping down the crater fell
  asleep part-way and the Dark Matter splash failed at 0.29x.

From the second round, still true: a slide move when a sweep is stopped
stretched a figure by 129 units; sweeping bodies that already touch a
surface, and sweeping off the line of motion, collapsed corpse travel to 27
units; resting any quiet rigid body with legacy's test gave 293 sleep and
wake cycles on the sloped stacks.

Not done at all: a windowed session watched in motion by a person (the
visual review above is stills); anything on ARM64 hardware, or an ARM64
client.
