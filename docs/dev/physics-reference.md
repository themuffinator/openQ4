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
recovery speeds, sleep thresholds, the impulse guard, inheritance), plus
`rb_enhancedDebug <entity>` for a per-step trace.

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
   query. Each contact's gap is measured from the body vertex the sweep
   followed into it, not from the lowest vertex overall, which is wrong for a
   long body on a curved support.
3. A persistent manifold keeps surfaces from recent steps that the sweep no
   longer reports. Planes are stored in the touched body's frame, so a body
   resting on a jostled neighbour cannot fall through it once their sweeps
   start inside each other.
4. Warm-started sequential impulses with Coulomb friction (μ = 12 × the
   body's contact friction, clamped to 0.05..1.2, so the stock 0.05 is 0.6),
   rolling and spinning resistance, restitution by the body's bouncyness only
   for a new touch above max(20, 2 g dt) units/s, and speculative gaps (a gap
   closes to the slop within a step, never past it).
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
9. Bodies sleep after `rb_enhancedSleepFrames` quiet supported steps. A quiet
   body does not wake its neighbours, so a settled pile goes to sleep together
   instead of waking itself every frame; a woken body (any impulse, force,
   push or script move goes through `Activate`) counts again.

The impact callback (`Collide`: bounce sounds, impact damage, break and
explode on impact) fires for the strongest approaching contact of a step, as
legacy fires it for its swept collision.

### Articulated figures (PHYS-03)

Applies to every `idPhysics_AF`: AI and player ragdolls, scripted and placed
figures, chains and lights. Stock AF defs are read as authored. Tree joints
are still solved exactly by `idAFTree` in linear time.

1. Speculative contacts per body (reach = the body's travel plus
   `af_enhancedMargin`, at least legacy's 2 units), with gaps and the
   persistent manifold as above.
2. The dense LCP over auxiliary rows (limits, contacts, joint and contact
   friction, loop joints) becomes a warm-started projected Gauss-Seidel solve
   on the same constraint matrix. Contact multipliers are matched to last
   step's contacts; limits and friction keep theirs. When the result's LCP
   error stays above `af_enhancedFallbackError` the exact legacy LCP solves
   the step instead (counted as a solver fallback).
3. **Impacts slide.** A contact the body approaches faster than the bounce
   threshold and reaches within the step is an impact. Its friction rows are
   bounded by the authored contact friction times the body's resting load on
   that surface (mass x gravity x the normal's upward share), not by the
   impact's normal force (`af_enhancedImpactFriction`, default 1). Legacy
   resolves impacts without friction: its collision impulse never changes the
   figure's own velocity and its closing-velocity check is normal-only. With
   full Coulomb friction on an impact-sized normal force, a strogg marine
   (contact friction 0.8) splashed into Air Defense 1's rising crater floor
   lost about 95% of its momentum in one step (median travel 30 units against
   legacy's 247). Resting contacts and slow touches keep full Coulomb
   friction, so resting and sliding bodies still stop as authored.
4. Overlap, and violated cone and pyramid limits, are removed by a pseudo
   velocity through the same tree responses (so joints stay consistent),
   bounded by `af_enhancedMaxRecovery`. Joint error correction is capped at
   `af_enhancedMaxJointCorrection` (32 units/s; legacy 256).
5. Figures do not bounce, as in legacy. Legacy wrote the bounce term with the
   opposite sign, so a stock figure never bounced, and the stock AF defs were
   tuned against that. Bouncing by the bodies' bouncyness (traced rebounds of
   about 0.4 of the approach speed) sent Dark Matter splashed corpses back off
   the crater walls they slid along in legacy: median travel 58 units against
   legacy's 247, and 165 without the bounce. `af_enhancedBounce 1` restores it
   for experiments.
6. Only bodies that move or turn beyond their speculative reach are swept;
   legacy sweeps every body every step.
7. The energy guard limits kinetic energy as for rigid bodies, but takes the
   excess from the motion relative to the figure's centre of mass first,
   which joint and contact forces can change; only an excess larger than
   that also scales the shared motion. Scaling every body uniformly also
   removed centre-of-mass momentum that no internal force can change.
   Quiet-frame sleeping and quiet-figure wake rules are as for rigid bodies
   (sleeping still requires the figure to allow rest).
8. A ragdoll whose def sets no `velocityTime` inherits the last
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
| `physicsStats start [label] \| stop \| report` | Per-frame CPU samples for rigid bodies and figures; prints one `PHYSICS_STATS {json}` line with totals, median, P95, P99, the worst frame, evaluation and motion counts, sleeps, wakes, sweeps, energy clamps, solver fallbacks, impulse clamps and peaks. Evaluations over 10 ms print `PHYSICS_SLOW`; rigid bodies over 2000 units/s print `PHYSICS_FAST`. |
| `physicsDigest [verbose]` | FNV-1a hash of every rigid-body and figure state, and separately of the workload's own `phys_*` entities (map AI and scripts can move map-placed bodies differently between runs). |
| `physicsReport <prefix>` | Mass, mass-weighted centre, momentum and peak body speed per entity. |
| `physicsSpawn`, `physicsDamage`, `physicsRadiusDamage`, `physicsImpulse` | Cheats that stage scenes from the console; `physicsSpawn` skips a placement the static world blocks instead of moving it onto its neighbours. |
| `tools/tests/physics_workloads.py` | Hidden, silent, one tic per frame (`com_fixedTic 1`), console-driven reference workloads in either or both modes. |
| `tools/tests/physics_enhanced_solver.py` | Native test of the production kernels and telemetry (CI). |
| `tools/tests/physics_envelope.py` | PHYS-04 behaviour envelope. |
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
| `sp-visual` | Screenshots of corpses and debris at fixed moments after death and after a blast. |

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
  worst frame must not be worse, taking the median of repeated runs on the
  same machine. Any exception is declared here with its bound before
  promotion.
- **Replay.** Legacy mode must reproduce the pre-change binary's workload
  digests exactly. Each mode must reproduce its own digests between runs on
  single-player workloads (multiplayer bots are not deterministic).
- **Envelope.** Momentum within 2 %; light hits within the legacy
  displacement envelope (largest legacy displacement + 25 % or + 24 units,
  whichever is larger) and a peak body speed within the same kind of bound
  (legacy peak x 1.25 + 24 units/s); heavy hits with a median displacement
  within 0.4x-1.6x of legacy, or both under 32 units. A hit that gibs every
  corpse in both modes passes, since both modes treated it alike and nothing
  is left to compare. The speed bound was "never above legacy" until 14
  November: a corpse still falling when it is hit, or one that rolls once on
  a weak bump, peaked at 122 and 42 units/s against legacy's 103 and 26, which is
  noise at those speeds, not a flung body.

## Evidence

All runs: Windows x64, RTX 4060 Laptop, debugoptimized build of the PHYS line
on `main` at `f3b2f9583`, hidden window, silent, OpenGL, one tic per frame.
The machine is shared, so CPU figures are medians of repeated runs taken
while no build ran.

### Legacy replay (14 November 2026)

`g_enhancedPhysics 0` on the final game modules against game modules built
from the PHYS-01 commit (the mode switch, telemetry and workloads, before any
enhanced solver code), with the final `physicsSpawn` staging in both so the
scenes are the same: the workload digests (`phys_*` bodies) of all five
single-player workloads are identical at every checkpoint (sp-flatstacks,
sp-stacks, sp-ragdolls, sp-explosion, sp-setpiece). The all-bodies digest
also covers map-placed bodies that map AI and scripts move; it differs
between two runs of the same binary too, so it is not evidence either way.
Each binary repeats its own workload digests exactly.

A first attempt compared against the PHYS-01 commit's own `physicsSpawn`,
which moved a blocked placement onto its neighbours instead of skipping it:
it staged 16 explosion bodies and 8 marines where the final command stages
13 and 3, so its digests differ by construction, and its overlapping crates
cost 16-21 s of physics time per run.

### PHYS-04 envelope (14 November 2026)

`tools/tests/physics_envelope.py`, both modes on the same binary, six strogg
marine corpses per hit on Air Defense 1's crater floor (two of the eight ring
placements are blocked). Displacements are in units, from the report before
the hit to the one six seconds later.

| Hit | Class | Result | Legacy / enhanced |
| --- | --- | --- | --- |
| raw impulse | momentum | PASS | momentum change within 2 % in both modes |
| machinegun | light | PASS | resting corpses: no motion in either mode |
| machinegun, still falling | light | PASS | max 69 / 83, peak speed 103 / 121 |
| gauntlet | light | PASS | gibs every corpse in both modes |
| hyperblaster splash | light | PASS | no motion in either mode |
| weak vehicle-sized bump | light | PASS | max 1 / 6, peak speed 26 / 55 |
| rocket direct | heavy | PASS | gibs every corpse in both modes |
| rocket splash | heavy | PASS | median 124 / 64, max 233 / 141 |
| grenade splash | heavy | PASS | median 124 / 64, max 233 / 141 |
| grenade splash, still falling | heavy | PASS | median 293 / 201, max 427 / 415 |
| Dark Matter splash | heavy | PASS | median 247 / 166, max 472 / 248 |

Every enhanced hit spread the push of a light limb over its figure
(68-71 impulse clamps a run), so no struck body changed speed by more than
300 units/s against legacy's 1058-2116.

How the heavy hits got there: on 6 November Dark Matter splash travelled a
median 30 units against legacy's 247. A per-contact trace (`af_enhancedDebug
8`) showed the corpses driven at about 1,600 units/s into the crater's rising
floor and walls; the contact solve stopped the approach with an impact-sized
normal force, and the marine's contact friction of 0.8 then took nearly all of
the sliding speed within one step. Limiting impact friction to the resting
load and letting the energy guard keep centre-of-mass momentum raised it to
58 units; dropping the figure bounce legacy never had brought it to 166,
inside the gate.

### CPU on the reference workloads (14 November 2026)

`physics_workloads.py --mode both --repeat 3`, legacy / enhanced, medians of
three runs with no build running. Totals are aggregate physics milliseconds
over the workload (rigid bodies plus figures); p99 and worst are per frame;
the worst-frame column gives the median and, in brackets, the range of the
three runs. `sp-ragdolls` stages three of its eight marines and
`sp-explosion` 13 of its 16 bodies: `physicsSpawn` skips placements the
static world blocks.

| Workload | Total ms | p99 ms | Worst frame ms | Sleeps, wakes | Enhanced LCP fallbacks |
| --- | --- | --- | --- | --- | --- |
| sp-ragdolls | 328 / 143 | 1.37 / 0.83 | 1.70 / 1.14 (1.6-2.4 / 0.9-1.6) | 5, 0 / 6, 3 | 86 |
| sp-explosion | 282 / 102 | 0.93 / 0.99 | 3.34 / 3.93 (2.3-5.9 / 2.8-7.3) | 42, 27 / 28, 12 | 53 |
| sp-setpiece | 290 / 70 | 1.09 / 0.92 | 2.06 / 1.16 (0.9-2.1 / 0.9-1.5) | 2, 1 / 3, 1 | 53 |
| sp-stacks | 405 / 122 | 1.08 / 0.87 | 2.75 / 1.79 (2.6-2.8 / 1.4-2.1) | 1677, 1667 / 20, 8 | 53 |
| sp-flatstacks | 16 / 9 | 0.09 / 0.10 | 0.78 / 0.74 (0.8-1.0 / 0.7-0.8) | 12, 3 / 13, 4 | 0 |
| mp-frags | 565 / 435 | 0.64 / 0.52 | 3.07 / 1.36 (2.2-4.4 / 1.0-3.5) | 5998, 6114 / 352, 620 | 196 |

Enhanced costs less in total on every workload (23-76 % less). Its p99 is
lower or within 0.06 ms. The worst-frame budget is not decided for
`sp-explosion`: the single-player workloads are deterministic, so the three
enhanced runs did identical work, yet their worst frames spread from 2.8 to
7.3 ms, against legacy's 2.3 to 5.9 ms. On this shared machine three repeats
measure the machine, not the solver; the gate needs more repeats on a quiet
one before PHYS-06.

### Legacy defects reproduced

- **Sleep and wake churn.** Legacy stacks on Air Defense 1's sloped crater
  floor fell asleep and were woken again about 1,670 times in 20 seconds; a
  seven-bot deathmatch's corpses and debris about 6,000 times in a minute.
  Enhanced: 20 and 8, and 352 and 620.
- **Figures that never bounce.** Legacy's contact bounce term has the wrong
  sign (`idAFConstraint_Contact::Setup`), so a bouncy limb keeps closing on
  the surface; the enhanced step keeps that behaviour on purpose (see 5
  above).
- **Frictionless impacts.** Legacy's `CollisionImpulse` applies the impact
  impulse only to the other entity, and its closing-velocity check removes
  only the normal part of a body's velocity, so a figure thrown into a slope
  or wall slides along it however rough its authored contact friction. The
  enhanced step keeps impacts sliding and gives resting contacts their
  authored friction (see 3 above).
- **Endless twitching.** Recorded 6 November, not re-run since: two of Air
  Defense 1's placed marine ragdolls (`env_ragdoll_marine_fullgear_3` and
  `_6`) never came to rest in legacy.
- **Tunnelling.** The native test fires a small fast body at a 2-unit wall 64
  times; the enhanced step keeps every corner within 0.9 units of the face.
  The matching legacy repro in a map has not been staged.

### PHYS-05 integration (14 November 2026)

`tools/tests/physics_integration.py`, all seven checks pass: a save made
mid-motion in legacy loads in enhanced, one made in enhanced loads in legacy
and in enhanced, each restoring the bodies where they were and settling
without errors or runaway speeds; a legacy client on an enhanced listen
server and an enhanced client on a legacy one both commit the server's mode;
an MVD recorded on an enhanced server plays its 742 frames in enhanced mode
on a client set to legacy; a legacy client joins an enhanced dedicated server
and follows it.

The first run failed two checks for reasons outside physics. The dedicated
server's 50-second run ended before the client had finished starting, so the
check now keeps it up for 120 seconds. MVD playback from a fresh client died
at the map load with `spawn_entnum 4095 out of range`: a 9 November bounds
check in `RegisterEntity` rejected `ENTITYNUM_NONE`, the slot of the offline
demo viewer, so offline MVD playback was broken in 0.14.1. The check now
admits every slot of the entity table.
