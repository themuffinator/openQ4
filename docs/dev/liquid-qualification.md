# Liquid navigation qualification

MP-06 in the [roadmap](roadmap.md) owns delivery scope. This records what the
compiler and gameplay checks establish; the archived liquid plan preserves
implementation history.

## AAS liquid flags, 28 October 2026

Area emission now derives `AREA_LIQUID` from `AREACONTENTS_WATER`.
`Reachability_Swim` already tests water contents, while the water-jump builder
requires the liquid flag. Previously the compiler never supplied that flag.
The native `aas_liquid_area_state.py` fixture executes production area emission
for wet/dry, floor/no-floor, reused and empty BSP nodes and rejects the old
compiler behavior.

The actual staged dedicated compiler was independently copied into the fixed
`.tmp/runtime/compiler/` runtime because AAS output is written through
`fs_cdpath`, beside the executable. Tests must not write navigation output into
the release-style `.install/` tree or a retail installation. Engine, game,
package and dependency hashes are recorded before and after. No installed
assets were copied into a writable test profile or hard-linked.

The repository's authored `liquid_lab.map` supplies the geometry/materials. A
test-only AAS definition uses a 32x32x80 agent, swimming enabled, gravity 800,
16-unit steps and 20-unit water jumps. A pillar in the deep pool creates
adjacent convex water areas so swim edges are needed. The generated source,
settings, compiled AAS and logs are retained under
`.tmp/mp-completion-20261028/aas-liquid-partitioned/`.

| Observation | Result |
|---|---|
| Dedicated compiler process | Exit 0, completion marker, no fatal/error |
| Serialized areas | 87 total; all 9 water areas also have `AREA_LIQUID` |
| Swim reachabilities | 26 |
| Water-jump reachabilities | 16 |
| Runtime mutation | None; engine/module/packages retain their recorded hashes |

Two earlier diagnostic runs remain separate. `aas-liquid/` used a differently
named definition which duplicated `aas_types`; the shipped definition won, so
the requested custom AAS file was not produced. `aas-liquid-enabled/` uses the
correct override and emits 12 water jumps, but its three separate convex pools
need no intra-pool swim edges. Its harness correctly retained a failed
swim-edge expectation; adding the partitioned topology tests that generator.
Compiler precache/material-stage warnings remain in the logs and are not
presented as a warning-free run.

This qualifies compiled liquid classification and swim/water-jump production.
It does not establish the game AAS routing/movement consumers. Stock AAS
definitions explicitly disable swim reachabilities; those defaults are
preserved. Multiplayer bots use their own runtime collision-derived navigation
rather than these AAS files. On 28 October the user selected static liquids,
player/bot swimming and replicated air readouts. Swimming monsters, new swim
animations and moving volumes are deferred. Air-HUD replication and broader
visual/audio/physics qualification remain in the selected MP-06 scope.

## Replicated air and bot swimming, 2 November 2026

Air readouts are replicated. The server publishes each player's air
percentage as an entity event: a change at most every 100 ms, an unchanged
value once a second (`mpAirPublishClock`). Clients and spectators show the
replicated value (`mpAirReadoutState` refuses out-of-order and conflicting
stamps), and a server demo's first snapshot bootstraps every player's readout.
`player_air_readout_state.py` executes the ordering, reset and codec rules with
reversal controls.

`mp_liquid_air_smoke.py` now passes on Windows x64 with OpenGL and the retained
HUD (`.tmp/wt-mp05-tools/air3/`). A listen host is placed under water in the
liquid lab and recorded; a separate observer process follows it.

- The observer received the host's air as it drained and recovered (81, 72,
  64, 55, 47 and 43 per cent, then back up to 70) with advancing server stamps.
- `s_soundWorlds` reported the underwater muffle on, then off after surfacing.
- Paused demo seeks to 1, 7, 1 and 7 seconds restored 100, 56, 100 and 56 per
  cent, and the followed player's HUD showed the same values.
- The submerged engine capture shows the AIR gauge (74) and the underwater
  view.

The harness had called a HUD probe command that does not exist and bound every
interface; it now uses `ui_hudTest guis/hud.gui get player_air` and binds to
loopback, so it runs from any runtime path without a firewall prompt.

A 90-second audit of six skill-4 bots in the liquid lab on the same build
(`mp_bot_audit.py --map mp/liquid_lab --classify-impulses`,
`.tmp/wt-mp05-tools/liquid-audit/`) simulated 82.0 seconds: 53 kills, no world
deaths, one stuck event, no goal give-ups and 19 completed traversals.

Still open: swimming under player control (it needs real input, which is never
synthesised), projectiles entering and leaving liquids, and splash, bubble and
wade presentation checked by eye. Swimming monsters, new swim animations and
moving liquid volumes stay deferred.

## Projectile liquid crossings, 3 November 2026

`g_debugLiquid 1` now also logs each projectile surface crossing on the server,
with the projectile, entered or left, the liquid contents, the boundary point
and the speed. The line follows the shared splash call in the same branch, so
every logged crossing also played the splash. Clients never detect crossings
themselves; they receive the server's splash.

A 120-second audit of six skill-4 bots in the liquid lab
(`mp_bot_audit.py --map mp/liquid_lab --set g_debugLiquid=1`,
`.tmp/wt-mp01-tools/liquid-proj/`) simulated about 87 seconds: 52 kills, no
world deaths and one stuck event. It logged 58 crossings:

| Projectile | Entered | Left |
| --- | --- | --- |
| Hyperblaster bolt | 23 | 23 |
| Rocket | 10 | 2 |
| By liquid | water 28, lava 3, slime 2 | water 25 |

- Every exit came from a shot fired under water: projectiles launched inside a
  pool travel out of it.
- Entries carried on to impacts below the surface; liquid stops nothing.
- Crossing speeds follow each weapon's own speed curve: hyperblaster bolts at
  1457-1950 units/s as they accelerate, rockets at 518-935. The crossing leaves
  velocity untouched.
- No projectile crossed a pool and out again within one second; the lab's
  pools are deep enough that a shot fired into one ends on its floor.

Still open: swimming under player control (real input only) and splash, bubble
and wade presentation checked by eye.
