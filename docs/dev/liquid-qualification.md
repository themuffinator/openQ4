# Liquid navigation qualification

## Current MP regression run, 3 November 2026

The staged MP roadmap build passes `mp_liquid_air_smoke.py --renderer gl`
in `.tmp/mp-roadmap/liquid/`. A remote spectator receives changing air values,
the sound-world diagnostic changes its underwater state, and four paused demo
seeks restore air and the followed HUD to 100, 56, 100 and 56. The engine's
submerged render-target capture was inspected: the underwater view and the
AIR gauge (75 at capture time) are visible. Both processes exit cleanly and
runtime hashes stay unchanged. The tested client SHA-256 starts
`241211745bb14c70`; the report retains full identities.

This is regression evidence for implemented replication/replay and the
underwater HUD. It does not qualify player-controlled swimming, audible muffle,
or the outstanding splash/wade presentation; those gates remain open.

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

## Splash and bubble presentation by eye, 5 November 2026

`mp_liquid_effects_capture.py` drops a god-mode listen host, in third person,
into the liquid lab's deep water, slime and lava pools. It writes bursts of
engine captures through each surface, holds the host on the deep pool's floor
and lifts it back out. `g_debugLiquid` ties each burst to the server's entry,
submersion and exit transitions, and `bse_debug 1` lists every effect the
client actually plays. No operating-system input is used.

The first runs found a presentation defect. The water entry splash
(`splash_lrg`) rises clearly at the surface where the player crosses it. The
slime and lava splashes played, but nothing showed in any entry frame.
`slime_pool.fx` is one flat 30-unit surface quad that fades in, and
`putra/acid_splash.fx` is a drip-sized splash with 3- to 6-unit lines. The
shared liquid definition now uses player-scale stock effects for player and
monster entries: `levels/tram/pipesplash.fx` for slime (green bubble burst,
shockwave and drifting green steam) and `levels/waste/wastesplash.fx`
for lava (dark-red spray and steam). Projectile crossings keep their separate,
smaller `fx_impact_*` effects.

The run on the rebuilt package passes (`.tmp/mp-side/liquid-fx-r5/report.json`,
SHA-256 `3efc22034a6725f91ed4d3fb01a43f95ee53e2d072eb1c34cafd1a1fa15308d4`;
client `d4e78340…f599`, `game-mp_x64.dll` `e0ff8c70…ce7e`, `pak0.pk4`
`cedebec8…8ac2`, OpenGL). It plays `splash_lrg` and the head bubble trail
`recomp_bubble` in water, `tube_goo_splash` in slime and `wastesplash` in lava,
and inspected captures show:

- **Water:** a white splash column rises from the crossing point as the player
  enters. The next frame is the tinted, refracted underwater view.
- **Slime:** a green goo plume with droplets rises over the entry point.
- **Lava:** a dark-red spray with steam rises over the entry point. It is subtler
  against the bright surface, but visible, where the old effect showed nothing.
- **Bubbles:** the stock trail spawns tiny 2- to 6-unit bubbles across a
  200-unit sphere. At 170 units in third person only a few faint green points
  near the head can be told from the underwater material's own specks, which
  remain in a control capture with effect rendering off. Effect logging, not
  the capture, establishes that the trail plays.

On 6 November the slime entry moved from `tube_goo_splash` to
`levels/tram/pipesplash.fx`. The retail `tube_goo_splash` stain names the decal
material `textures/decals/goospread`, which no Quake 4 PK4 ships (only
`goospread2` exists). Because the liquid definition is precached, every map load
logged a missing-image warning. That failed the renderer harnesses that require
a clean log. Every material `pipesplash` uses ships with the game. The rerun
(`.tmp/mp-side/liquid-goo/report.json`, SHA-256
`26bf1658fd932373ee2c0b8ddedc603beed792916bb13bedf660f6ce263984be`; client
`1e686197…1a04`, `pak0.pk4` `e4d5f8a8…1b23`, OpenGL) plays `pipesplash` on slime
entry and exit and logs no `goospread` warning, where the r5 log had two. Its
entry captures show a drifting green steam burst over the crossing point. The
bubble burst is hard to tell apart from the bright slime surface.

The hazard pools are 64 units deep over a raised floor, so a standing player is
neck-deep, not submerged, and has no bubble trail there. Wading has no visual
effect. It is the `snd_wade` sound while moving, which needs real movement and
stays with the [human checklist](mp-human-qualification.md#mp-06-swimming-and-liquid-effects-by-eye),
as do swimming under player control and a by-eye pass at normal play distances.
