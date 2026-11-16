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
9. Bodies sleep after `rb_enhancedSleepFrames` quiet supported steps. A body
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
   `af_enhancedMargin`, at least legacy's 2 units), with gaps and the
   persistent manifold as above.
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
   legacy sweeps every body every step.
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
| `physicsQuality <prefix \| *> [eye]` | Per entity: the largest joint anchor separation (stretch), the joints with static geometry between a body's centre and the joint (pierced: a limb through a wall or floor, with a `PHYSICS_PIERCED` line naming the joint), the bodies whose centre is inside a solid brush (embedded), whether it is awake and its fastest body. With `eye`, also whether static geometry hides it from the player's eyes. |
| `physicsWallProbe <yawFrom> <yawTo> <step> [pitch]` | Distance, thickness and incidence of the first static surface along each direction from the player's eyes: where to aim a tunnelling repro. |
| `physicsSpawn`, `physicsLaunch`, `physicsDamage`, `physicsRadiusDamage`, `physicsImpulse` | Cheats that stage scenes from the console. `physicsSpawn` places entities in a ring, line, grid or stack, or with their backs to the wall ahead (`wall`), and skips a placement the static world blocks instead of moving it onto its neighbours. `physicsLaunch` fires one entity along the view at a given speed and prints the surface it is aimed at. `physicsImpulse <prefix> * ...` reaches every body a prefix names. |
| `tools/tests/physics_workloads.py` | Hidden, silent, one tic per frame (`com_fixedTic 1`), console-driven reference workloads in either or both modes. `--frames` records every frame's cost and reduces repeats to a per-frame noise floor; `--cache-root` shares the generated-image cache between runs. |
| `tools/tests/physics_enhanced_solver.py` | Native test of the production kernels and telemetry (CI). |
| `tools/tests/physics_defects.py` | PHYS-01 defect repros in stock maps, both modes. |
| `tools/tests/physics_envelope.py` | PHYS-04 behaviour envelope: `--scene ai`, `moveables`, `mp` or `awakening`. |
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
  resting corpse that a light hit does not move is judged on one.
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
  at or outside the world.

History of the tolerances: the light-hit speed bound was "never above
legacy" until 14 November (a corpse still falling when it is hit, or one that
rolls once on a weak bump, peaked at 122 and 42 units/s against legacy's 103
and 26, which is noise at those speeds, not a flung body). Stagings, the
falling bound and the stretch rule date from 16 November.

## Evidence

Windows x64, RTX 4060 Laptop, debugoptimized build of worktree snapshot
`48730095e` (the PHYS line merged with `main` at `f28e8ced0`; the binaries
report `0.14.1-dev+gd522613e`, the commit they were configured at), hidden
window, silent, OpenGL, one tic per frame. The machine is shared and was at
full load throughout, with three of these clients running at once from
separate copies of the staged runtime; CPU figures use the noise floor.
Linux and ARM64 figures are in their own section. Everything here is from 16
November 2026 unless a date says otherwise. Raw outputs are under
`.tmp/phys/` in the build worktree and are not kept in the repository.

### Legacy replay

`g_enhancedPhysics 0` on game modules built from the PHYS-01 commit (the
mode switch, telemetry and workloads, before any enhanced solver code) and on
the 14 November modules, with the same `physicsSpawn` staging in both:
the workload digests (`phys_*` bodies) of all five single-player workloads
were identical at every checkpoint. The all-bodies digest also covers
map-placed bodies that map AI and scripts move; it differs between two runs
of the same binary too, so it is not evidence either way.

On 16 November the final build's legacy digests were compared with the ones
recorded on 14 November: identical on all five workloads at every checkpoint.
This round's solver changes are all inside the enhanced branches; the shared
wrappers gained only telemetry. Each mode repeats its own digests over five
runs on Windows and three on Linux x64. Windows and Linux digests differ from
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
| Resting jitter: crate and barrel stacks on the sloped crater floor, nudged, read from 12 to 20 seconds | 1,767 sleeps and 1,757 wakes; one body still awake | 20 sleeps and 8 wakes; none awake; no drift | Legacy shows it; enhanced does not |
| Twitching: the eight placed marine ragdolls, nudged on the chest | One still awake after 10 seconds, all at rest after 30 | All at rest after 10 seconds | Not reproduced as endless; enhanced settles sooner |
| Sliding corpses: six fresh corpses on the crater slope, read from 4 to 8 seconds (the game removes them at about 10) | No drift | No drift | Not reproduced |
| Stuck limbs: six marines killed with their backs to a wall, then thrown by a Dark Matter splash | No pierced joints, no embedded bodies | None | Not reproduced in legacy; enhanced equal |
| Joint stretch: six resting corpses thrown by a Dark Matter splash, read every second frame | Largest 85 units; more than 8 units apart for 42 frames | Largest 106 units; more than 8 units apart for 18 frames | Legacy shows it; enhanced is shorter, not gone |
| Tunnelling: a marine helmet fired at the terrain at 600 to 9,000 units/s, straight on to 30 degrees off, 18 launches | None behind the surface | None | Not reproduced; the native test covers the kernel |
| Placed ragdolls: the eight placed marines settling from the map's pose | No pierced joints; none sinks | `env_ragdoll_marine_fullgear_6` sinks 7.9 units and ends with two joints pierced | **Enhanced is worse** (open, see Known defects) |

How three of these got there:

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

### Behaviour envelope (PHYS-04)

`tools/tests/physics_envelope.py`. Legacy / enhanced; travel in units from
the report before the hit to the one six seconds later; heavy and falling
classes pooled over three stagings.

**Strogg marine corpses on Air Defense 1's crater floor (`--scene ai`): all
classes pass.**

| Hit | Class | Result | Corpses | Median travel | Largest travel | Peak speed | Largest stretch, frames visible |
| --- | --- | --- | --- | --- | --- | --- | --- |
| raw impulse | momentum | PASS | 6 / 6 | momentum change within 2 % in both modes | | | |
| machinegun | light | PASS | 6 / 6 | 0 / 0 | 0 / 0 | 0 / 0 | 13 for 3 / 11 for 5 |
| machinegun, still falling | light | PASS | 17 / 17 | 60 / 56 | 91 / 119 | 125 / 161 | 25 for 14 / 20 for 13 |
| gauntlet | light | PASS | gibs every corpse in both modes | | | | |
| hyperblaster splash | light | PASS | 6 / 6 | 0 / 0 | 0 / 0 | 0 / 0 | 13 for 3 / 11 for 5 |
| weak vehicle-sized bump | light | PASS | 6 / 6 | 1 / 1 | 1 / 17 | 26 / 41 | 13 for 3 / 11 for 5 |
| rocket direct | heavy | PASS | gibs every corpse in both modes | | | | |
| rocket splash | heavy | PASS | 17 / 17 | 110 / 63 | 233 / 156 | 864 / 897 | 34 for 158 / 28 for 30 |
| grenade splash | heavy | PASS | 17 / 17 | 110 / 63 | 233 / 156 | 864 / 897 | 34 for 158 / 28 for 30 |
| grenade splash, still falling | heavy | PASS | 17 / 17 | 274 / 241 | 461 / 457 | 1,156 / 1,066 | 37 for 87 / 118 for 50 |
| Dark Matter splash | heavy | PASS | 17 / 17 | 231 / 139 | 472 / 492 | 1,754 / 1,579 | 88 for 113 / 116 for 59 |

The stretch columns include the quiet kill that stages each scene (13 units
for 3 frames in legacy). Every enhanced splash spread the push of a light limb
over its figure (193 or 194 impulse clamps over three stagings), so no struck
body changed speed by more than 300 units/s against legacy's 1,058 to 2,116.

**Where a thrown corpse loses its speed.** Right after a splash both modes
carry the same momentum: a rocket splash leaves each resting corpse's centre
of mass at about 800 units/s, a Dark Matter splash at about 1,600. Six frames
later legacy still has roughly 60 % of it; enhanced, with impact friction
bounded by the resting load as it was until 16 November, had 30 %. A corpse hit in the air lands as far in either mode (0.97x to
1.02x of legacy's mean travel in every variant measured), so the loss is in
the launch off the ground. Measured over three stagings, as enhanced mean
travel over legacy's:

| Enhanced variant | Rocket splash | Dark Matter splash | Grenade splash, falling | Machinegun, falling |
| --- | --- | --- | --- | --- |
| Impact friction bounded by the resting load (14 November rule), stopping surface in the manifold, position-only recovery | 0.55 | 0.49 | 0.96 | 1.10 |
| The same with fast-sliding contacts keeping half their friction | 0.55 | 0.58 | 1.04 | 1.13 |
| The same with a quarter | 0.55 | 0.56 | 1.03 | 1.11 |
| Frictionless impacts | 0.50 | 0.71 | 0.99 | 1.12 |
| Frictionless impacts, recovery swept on its own (shipped) | 0.65 | 0.71 | 1.02 | 1.16 |

Sliding friction does not decide it, so the sliding factor was dropped.
Friction on the landing contacts does for the faster hit: a thrown corpse
touches down on several bodies at once with up to three contacts each, and
one resting load of friction on each is far more than legacy's none. What
remains is momentum legacy keeps because its 256 units/s joint correction
adds energy while its stopped limbs catch up, which the product rules do not
allow enhanced mode to copy. Corpses launched off the ground therefore travel
about two thirds as far as in legacy, inside the 0.4x gate.

**Crates and barrels on the same floor (`--scene moveables`): the splash
classes fail.**

| Hit | Class | Result | Bodies | Median travel | Largest travel | Peak speed |
| --- | --- | --- | --- | --- | --- | --- |
| raw impulse | momentum | PASS | 6 / 6 | momentum change within 2 % in both modes | | |
| machinegun, gauntlet, hyperblaster splash, rocket direct | light and heavy | PASS | 6 / 6 or 18 / 18 | 0 / 0 (damage does not push these bodies in either mode) | | |
| weak vehicle-sized bump | light | PASS | 6 / 6 | 38 / 11 | 208 / 152 | 128 / 128 |
| rocket splash | heavy | **FAIL** | 18 / 18 | 57 / 22 | 222 / 143 | 297 / 296 |
| grenade splash | heavy | **FAIL** | 18 / 18 | 57 / 22 | 222 / 143 | 297 / 296 |
| Dark Matter splash | heavy | **FAIL** | 18 / 18 | 133 / 40 | 263 / 140 | 341 / 419 |

Enhanced crates and barrels start as fast as legacy's and stop in a third of
the distance (0.38x and 0.30x of legacy's median). This is the rigid-body
step, which this round did not change apart from the rest test. See Known
defects for what was tried.

**The Awakening's grunts on `m01_stranarus_trench2` (`--scene awakening`,
`fs_game q4xbase`): seven of ten classes pass.**

| Hit | Class | Result | Corpses | Median travel | Largest travel | Peak speed | Largest stretch, frames visible |
| --- | --- | --- | --- | --- | --- | --- | --- |
| raw impulse | momentum | PASS | 7 / 7 | momentum change within 2 % in both modes | | | |
| machinegun | light | PASS | 7 / 7 | 0 / 0 | 0 / 1 | 4 / 8 | 37 for 47 / 8 for 0 |
| machinegun, still falling | light | PASS | 22 / 22 | 39 / 36 | 256 / 266 | 168 / 182 | 37 for 54 / 18 for 14 |
| gauntlet | light | PASS | 7 / 7 | 0 / 0 | 0 / 1 | 4 / 8 | 37 for 47 / 8 for 0 |
| hyperblaster splash | light | PASS | 7 / 7 | 0 / 0 | 0 / 1 | 4 / 8 | 37 for 47 / 8 for 0 |
| weak vehicle-sized bump | light | **FAIL** | 7 / 7 | 0 / 1 | 1 / 223 | 7 / 17 | 37 for 47 / 8 for 0 |
| rocket direct | heavy | PASS | 22 / 22 | 0 / 0 | 0 / 5 | 4 / 11 | 37 for 54 / 18 for 14 |
| rocket splash | heavy | **FAIL** | 22 / 22 | 41 / 66 | 345 / 287 | 556 / 587 | 37 for 89 / 20 for 32 |
| grenade splash | heavy | **FAIL** | 22 / 22 | 41 / 73 | 345 / 278 | 556 / 605 | 37 for 89 / 20 for 52 |
| grenade splash, still falling | heavy | PASS | 22 / 22 | 145 / 159 | 331 / 330 | 652 / 644 | 37 for 96 / 113 for 98 |
| Dark Matter splash | heavy | PASS | 22 / 22 | 144 / 143 | 489 / 406 | 1,068 / 1,082 | 37 for 173 / 34 for 64 |

On this terrain the splash medians are above legacy's, 1.6x and 1.8x against
a 1.6x bound, and one of seven bumped corpses left its spot: it moved further
than legacy's (legacy's largest is 1 unit) and went over the trench's edge.
Legacy's quiet kill alone stretches a grunt by 37 units for 47 frames;
enhanced by 8 for none.

**Player corpses on `mp/q4dm1` (`--scene mp`, a listen server with six
paused bots, three stagings): all classes pass.**

| Hit | Class | Result | Corpses | Median travel | Largest travel | Peak speed | Largest stretch, frames visible |
| --- | --- | --- | --- | --- | --- | --- | --- |
| raw impulse | momentum | PASS | 6 / 6 | momentum change within 2 % in both modes | | | |
| machinegun | light | PASS | 18 / 18 | 0 / 0 | 0 / 1 | 0 / 6 | 21 for 9 / 6 for 0 |
| machinegun, still falling | light | PASS | 18 / 18 | 55 / 55 | 375 / 481 | 761 / 226 | 87 for 45 / 13 for 5 |
| gauntlet | light | PASS | 18 / 18 | 0 / 0 | 0 / 0 | 0 / 0 | 21 for 8 / 6 for 0 |
| hyperblaster splash | light | PASS | 18 / 18 | 0 / 0 | 0 / 0 | 0 / 0 | 95 for 23 / 7 for 0 |
| weak vehicle-sized bump | light | PASS | 18 / 18 | 3 / 2 | 4 / 4 | 69 / 33 | 27 for 39 / 20 for 15 |
| rocket direct | heavy | PASS | 18 / 18 | 0 / 0 | 0 / 0 | 0 / 0 | 13 for 3 / 10 for 2 |
| rocket splash | heavy | PASS | 18 / 18 | 0 / 0 | 0 / 0 | 5 / 0 | 21 for 104 / 12 for 8 |
| grenade splash | heavy | PASS | 18 / 18 | 0 / 0 | 0 / 0 | 0 / 0 | 80 for 25 / 13 for 20 |
| grenade splash, still falling | heavy | PASS | 18 / 18 | 55 / 55 | 348 / 76 | 189 / 116 | 37 for 31 / 9 for 2 |
| Dark Matter splash | heavy | PASS | 18 / 18 | 0 / 0 | 25 / 1 | 28 / 25 | 80 for 31 / 14 for 15 |

A player's corpse at rest is not moved by a later hit in either mode; what
moves is the collapse itself, with the same median in both. Legacy stretches
a player corpse by 13 to 95 units for 3 to 104 frames over these hits;
enhanced by 6 to 20 units for at most 20 frames.

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
| Putra's 21 lights, light impulse | PASS | swing 0.02 | swing 0.05 |
| Putra's 21 lights, heavy impulse | **FAIL** | swing 0.55, all at rest after 15 seconds | swing 4.3 on one light, two still awake after 15 seconds |
| Putra's 21 lights, machinegun, rocket splash | PASS | no motion | no motion |
| Debris from four launching barrels | PASS | 1 piece, 220 units from the blast, 619 units/s | 1 piece, 210 units, 576 units/s |

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
legacy, 400 in enhanced; the retail maps' own, as recorded in the
[retail baseline ledger](retail-baseline-ledger.md)). The maps hold 1,675
simulated bodies between them. Over the 31 settle windows legacy spent 1,387
ms on physics and enhanced 359 ms. Bodies that left the world: 3 in legacy, 2
in enhanced. Joints pierced by geometry after settling: 11 in legacy, 13 in
enhanced; the two extra are Air Defense 1's placed marine (see Known
defects), and Putra's nine, Hub 1's one and Medical Labs' one are the same in
both. Waste is the one map where enhanced cost more (160 ms against 78, worst
frame 6.6 ms against 0.7, one run each on a loaded machine).

The first enhanced walk found two pickups out of place, which legacy leaves
where the map puts them: Network 1's `ammo_railgun_moveable_1` ended 822
units away and Core 2's `ammo_shotgun_14` 740. Enhanced rigid bodies waited
20 quiet steps before resting, where legacy rests a placed body on its first
step; in that window the pickup's support contacts aged out of the manifold
and it tipped off its perch. The rest test for bodies that have not moved
since they were created fixed both (Network 1: no motion in either mode;
Core 2: the same one ammo box settles 14 units against legacy's 17). Storage
2's `ammo_railgun_1` still settles 20 units from its spot in enhanced mode
and stays put in legacy.

**Set pieces.** Each is driven by the map's own trigger or script function,
in both modes; travel is each driven body's displacement after ten seconds.

| Set piece | Result | Legacy | Enhanced |
| --- | --- | --- | --- |
| Air Defense 1: the blasted grunt (`gruntBlasted`) | PASS | peak 1,041 units/s; the script freezes the body half a second later | peak 1,020 units/s; frozen the same way |
| Building B: the marine thrown at the wall (`ChuckBody1`, impulse 200,000) | PASS | travels 265 units, at rest | travels 247 units, at rest; four impulse clamps |
| Building B: the falling barrels (`Weld_guard`) | PASS | fall 266 and 216 units, at rest | fall 278 and 265 units, at rest |
| Process 1: the knocked-down grate (`knockdownGrate`, impulse 50,000,000) | PASS | falls 135.8 units, at rest | falls 135.7 units, at rest |
| Storage 2: the falling panel (`panel_fall`) | PASS | already on the floor when shown; no motion | the same |
| Hub 1: the grate (`grate`) | PASS | falls 215 units, travels 243, at rest | falls 200 units, at rest |

Vehicles, projectiles and flying AI on rigid-body physics keep the legacy
step in both modes by design, so the chain exercises them unchanged.

### Visual review

`sp-visual` and `sp-visual-rigid`: 17 moments, three view directions, both
modes, 102 frames read side by side as contact sheets. They are hidden-window
captures at 100 degrees field of view with the weapon hidden and brightness
raised, not a windowed session watched in motion.

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
both modes with no gameplay inside a draw. Posed figure frames were blended
between simulation steps 432 times of 464 in enhanced mode for the falling
ragdoll, 239 of 245 with debris flying and 598 of 641 for the death camera
(93 to 98 %); legacy 1,868 of 1,956, 2,157 of 2,206 and 1,759 of 1,853 (95 to
98 %). Enhanced poses a quarter as many figure frames over the same capture
because the map's other figures are asleep. Both steps run once per game tic,
so the sample cadence the presentation layer sees is unchanged.

The first integration run on 14 November failed two checks for reasons
outside physics. The dedicated server's 50-second run ended before the client
had finished starting, so the check now keeps it up for 120 seconds. MVD
playback from a fresh client died at the map load with `spawn_entnum 4095 out
of range`: a 9 November bounds check in `RegisterEntity` rejected
`ENTITYNUM_NONE`, the slot of the offline demo viewer, so offline MVD playback
was broken in 0.14.1. The check now admits every slot of the entity table.

### CPU on the reference workloads (PHYS-02, PHYS-03, PHYS-06)

`physics_workloads.py --mode both --frames`, legacy / enhanced. Totals are
aggregate physics milliseconds over the workload (rigid bodies plus figures);
p99 and worst are per frame. `sp-ragdolls` stages three of its eight marines
and `sp-explosion` 13 of its 16 bodies: `physicsSpawn` skips placements the
static world blocks.

**Windows x64, five runs each.**

| Workload | Total, floor | p99, floor | Worst frame, floor | Total, median | Worst frame, median (range) | Sleeps, wakes | Enhanced LCP fallbacks |
| --- | --- | --- | --- | --- | --- | --- | --- |
| sp-ragdolls | 267 / 83 | 1.07 / 0.60 | 1.32 / 0.76 | 365 / 107 | 7.20 / 5.75 (6.2-8.2 / 3.7-6.0) | 5, 0 / 6, 3 | 53 (2.8 ms) |
| sp-explosion | 264 / 46 | 0.80 / 0.59 | 2.40 / 2.45 | 328 / 60 | 5.49 / 4.09 (4.8-7.5 / 2.5-7.1) | 42, 27 / 28, 12 | 25 (1.2 ms) |
| sp-setpiece | 208 / 27 | 0.53 / 0.62 | 0.74 / 0.73 | 270 / 35 | 6.37 / 1.94 (3.4-8.8 / 0.8-9.0) | 3, 1 / 3, 1 | 25 (1.1 ms) |
| sp-stacks | 310 / 69 | 0.62 / 0.59 | 1.20 / 1.44 | 398 / 86 | 6.33 / 2.24 (5.0-8.8 / 1.5-3.1) | 1,677, 1,667 / 20, 8 | 25 (1.2 ms) |
| sp-flatstacks | 13 / 9 | 0.05 / 0.07 | 3.52 / 2.70 | 17 / 13 | 4.89 / 5.20 (3.5-9.5 / 2.7-7.9) | 12, 3 / 13, 4 | 0 |

- **Total.** Enhanced costs less on all five (31 to 87 % less on the floor).
  Most of that on the Air Defense 1 workloads is the map's own placed
  ragdolls: in legacy a figure moves in about 900 of the workload's steps, in
  enhanced in 97. Rigid bodies alone cost 68 ms against 27 on `sp-explosion`
  and 124 against 50 on `sp-stacks` (first run of each).
- **Worst frame.** Single runs spread from 0.8 to 9.5 ms for identical work,
  which is why the floor is used. On the floor enhanced is lower on three
  workloads, 0.05 ms higher on `sp-explosion` (the legacy floor there is one
  reading of five, between 2.4 and 6.7 ms; enhanced's five lie between 2.45
  and 3.9) and 0.24 ms higher on `sp-stacks` (legacy's five readings of its
  worst frame are 1.2 to 1.8 ms, enhanced's 1.44 to 2.05). The worst-frame
  budget is therefore met on three workloads, undecided on one and missed by
  a quarter of a millisecond on one.
- **Fallbacks.** The exact LCP solved 25 to 53 steps per workload instead of
  the iterative solve, for 1.1 to 2.8 ms: under 4 % of enhanced's time, so
  not where its cost is.

`mp-frags`, three runs (bots are not deterministic, so medians and ranges):

| | Legacy | Enhanced |
| --- | --- | --- |
| Total ms, median (runs) | 635 (383, 635, 699) | 569 (569, 590, 393) |
| p99 ms | 0.66 | 0.62 |
| Worst frame ms, median (range) | 3.30 (1.6-4.7) | 6.18 (1.5-9.2) |
| Sleeps, wakes (median run) | 16,025, 16,072 | 623, 804 |
| Largest joint stretch, frames visible (per run) | 18 for 41; 70 for 222; 50 for 128 | 12 for 9; 11 for 10; 14 for 17 |
| LCP fallbacks | | 162 (9.0 ms) |

The runs differ from each other by more than the modes do (one run's bots
throw ten times the debris of another's), so this shows no cost difference in
either direction and does not decide the worst-frame budget. What it does
show in every run is the sleep and wake churn and the stretched player
corpses of legacy, and neither in enhanced.

### Linux x64 and ARM64 (PHYS-05, PHYS-06)

**Linux x64.** WSL2, Ubuntu 24.04.3, GCC 13.3, debugoptimized, built from an
export of `48730095e`; the client runs under Xvfb on Mesa's software
renderer. Three runs each, legacy / enhanced; all 30 runs pass without
errors and each mode repeats its digests.

| Workload | Total, floor | p99, floor | Worst frame, floor | Figure steps that moved a figure | Sleeps, wakes | Enhanced LCP fallbacks |
| --- | --- | --- | --- | --- | --- | --- |
| sp-ragdolls | 112 / 133 | 1.04 / 1.23 | 1.09 / 1.42 | 504 / 707 | 7, 0 / 9, 3 | 50 (2.3 ms) |
| sp-explosion | 87 / 88 | 1.04 / 1.21 | 1.20 / 1.46 | 131 / 289 | 39, 22 / 31, 12 | 34 (1.8 ms) |
| sp-setpiece | 43 / 71 | 1.07 / 1.23 | 1.16 / 1.41 | 132 / 290 | 4, 1 / 6, 1 | 34 (1.8 ms) |
| sp-stacks | 138 / 98 | 1.02 / 1.22 | 1.26 / 1.44 | 131 / 289 | 2,199, 2,187 / 22, 7 | 34 (1.8 ms) |
| sp-flatstacks | 8.4 / 6.1 | 0.05 / 0.07 | 0.10 / 0.11 | 0 / 0 | 12, 3 / 13, 4 | 0 |

**The CPU budget is not met on Linux.** Enhanced costs 19 % more than legacy
on `sp-ragdolls` and 66 % more on `sp-setpiece`, the same on `sp-explosion`
and 28 to 29 % less on the two stack workloads; its p99 and worst frame are
about 0.2 ms higher on every Air Defense 1 workload. The figure-steps column
is why. Air Defense 1's own figures (eight placed marines and the grunt)
come to rest after 131 moving steps in legacy and 289 in enhanced on Linux;
on Windows they take about 900 in legacy and 97 in enhanced. A step that
moves a figure costs about the same in both modes on both platforms (0.26 to
0.34 ms), so each total follows how long the map's placed ragdolls take to
settle, and that differs by compiler in both modes. Rigid bodies alone cost
less in enhanced mode on Linux as on Windows (59 ms against 23 on
`sp-explosion`, 118 against 35 on `sp-stacks`, first run of each).

Bot rounds on Linux x64, three runs each (medians; bots are not
deterministic):

| | Legacy | Enhanced |
| --- | --- | --- |
| `mp-frags` (listen server) total ms, median (runs) | 1,770 (1,770, 1,889, 1,613) | 1,764 (1,764, 1,879, 1,613) |
| `mp-frags` p99, worst frame median (range) | 0.57, 1.21 (1.0-3.9) | 0.61, 1.76 (1.5-1.8) |
| `mp-frags` sleeps, wakes | 55,094, 55,288 | 3,143, 3,656 |
| `mp-frags` largest stretch, frames visible | 85-91 for 613-746 | 16-24 for 74-99 |
| `ded-frags` (dedicated server) total ms, median (runs) | 1,449 (661, 1,449, 1,610) | 1,673 (568, 2,088, 1,673) |
| `ded-frags` p99, worst frame median (range) | 1.87, 2.63 (1.5-3.4) | 2.40, 4.32 (1.2-6.9) |
| `ded-frags` largest stretch, frames visible | 50-68 for 118-326 | 19-49 for 50-72 |

The dedicated server runs the round in both modes with no renderer and no
errors. Its three runs differ by a factor of two and a half in both modes
for similar work (5,300 to 6,400 figure evaluations a run), so they show no
cost difference.

**Other Linux x64 checks.** `physics_integration.py` passes all seven
checks: saves made in either mode load in the other and in their own, clients
follow a listen server's mode both ways, an MVD recorded on an enhanced
server plays to its end, and a legacy client joins an enhanced dedicated
server. The save checks first failed for a reason outside physics: the test
tree was an export with five commits of history, a build stamps its save
with the commit count, and the save validator rejects a build number below
640, so that build could not restore its own saves. With the history padded
to the real count the same sources pass.

`physics_envelope.py --scene ai` on six classes, three stagings: all pass.
The two heavy classes are close to the bound on Linux, with a rocket splash
median of 144 / 61 units and a Dark Matter splash median of 205 / 84 (0.42x
and 0.41x of legacy against a 0.4x bound; 0.57x and 0.60x on Windows).
Machinegun into falling corpses 64 / 64, largest 106 / 124. Stretch after the
Dark Matter splash: 72 units for 198 frames in legacy, 54 for 95 in enhanced.
The moveables, multiplayer and Awakening scenes, the coverage and campaign
harnesses and the defect repros were not run on Linux.

**ARM64.** The AArch64 cross build (GCC, the CI options) of the dedicated
server and multiplayer game module runs `ded-frags` in both modes under
`qemu-aarch64` 8.2.2 user-mode emulation, without errors: 523 LCP fallbacks
in enhanced mode, largest stretch 70 units for 446 frames in legacy and 28
for 126 in enhanced. Emulated timings are not hardware performance and are
not recorded. The native solver test passes on six builds (x64 with g++ and
clang++, AArch64 with clang++ with and without floating-point contraction,
AArch64 with g++), printing the same values on all of them. No ARM64 client
was run, and nothing ran on ARM64 hardware.

### Known defects and what was tried

Open in enhanced mode on the final build; each is why an item below is not
closed.

1. **Blasted crates and barrels stop short.** After a rocket, grenade or Dark
   Matter splash they leave as fast as legacy's and travel 0.3x to 0.4x as far
   (moveables envelope). Crates and barrels map to a friction coefficient of
   1.2 (twelve times the authored 0.2, clamped). Tried and not kept: other
   friction scales (3, 4 and 6: weak and inside the staging noise); bounding
   impact friction by the resting load as figures did until 16 November (the
   sloped-floor stacks then creep); bouncing on every impact (no effect).
2. **A figure lying across a ledge edge sinks.** Air Defense 1's
   `env_ragdoll_marine_fullgear_6` sinks 7.9 units from the map's pose and
   rests with two joints pierced; Air Defense 2's
   `env_ragdoll_marine_fullgear_7` falls 134 units from where legacy leaves
   it. Contact rows are a vertex against a plane and cannot hold an edge on an
   edge. Tried and not kept: sweeping bodies that move less than their
   reach (it did not hold the edge), and sweeping the whole body whenever
   recovery moves it (more corpses stuck on uneven ground, see Defect
   repros).
3. **The Awakening's terrain.** Rocket and grenade splashes throw grunts 1.6x
   and 1.8x as far as legacy at the median (bound 1.6x), and a weak bump moved
   one corpse of seven 223 units over the trench's edge.
4. **Putra's lights after a heavy push.** One light swings 4.3 units against
   legacy's half a unit, and two of 21 are still awake after 15 seconds.
5. **A placed pickup still settles off its spot.** Storage 2's
   `ammo_railgun_1` ends 20 units away; legacy leaves it in place.
6. **CPU on Linux, and worst frames.** On Linux x64 enhanced costs more than
   legacy on two workloads and its heaviest frames cost about 0.2 ms more;
   on Windows its worst frame is 0.24 ms above legacy's on `sp-stacks`; Waste
   costs twice legacy's physics time in the campaign walk. The multiplayer
   workloads decide nothing either way.
7. **Corpses launched off the ground travel about two thirds as far** as in
   legacy (inside the gate; see the envelope).
8. **Which stack survives a knock differs** between the modes on the sloped
   floor (see Visual review). Not a defect by any gate, but a visible
   difference.

Solver experiments of this round that were measured and reverted, beyond the
ones above: a slide move when a sweep is stopped (with strong joint recovery
it stretched a figure by 129 units); a friction factor for fast-sliding
contacts (no effect on mean travel); sweeping bodies that already touch a
surface, and sweeping off the line of motion (travel collapsed to 27 units);
resting any quiet rigid body with legacy's test, not only undisturbed ones
(293 sleep and wake cycles on the sloped-floor stacks).

Not done at all: a windowed session watched in motion by a person (the
visual review above is stills); a Linux or ARM64 client run of the envelope,
coverage and campaign harnesses; anything on ARM64 hardware.
