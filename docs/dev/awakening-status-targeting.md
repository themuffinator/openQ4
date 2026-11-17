# Awakening status and targeting fixes

The October 2026 gameplay review identified seven concrete defects in the SP
implementation. The intended combat roles remain unchanged: freeze is temporary
crowd control with a delayed lethal shatter, the Retch provides one ally buff,
Pain Lord retains his phase rules, and cockpit missiles acquire valid targets.
This work adds no expansion multiplayer or proprietary assets.

| Priority | Fix | Regression coverage |
| --- | --- | --- |
| P1 | Persist freeze amount, hit location, timer, layered materials, buff ownership and animation multipliers | Actual status writer/reader round trip; old-format recovery; staged save/load |
| P2 | Run lethal ice conversion after the actor's damage gate; freeze an unprotected one-HP AI once | Full actor damage method with undying, stunned/processing Pain Lord, docked Valkaryne, zero damage and friendly fire |
| P2 | Compose temporary slowdown with authored and scripted playback rates | Active blends, new animations, 1.75 base multiplier, thaw and restore |
| P2 | Give each Retch buff explicit ownership independent of its shader | Two providers, chilled allies, both effect-removal orders, base overlays/scales and deferred restoration |
| P2 | Read target offsets from target actors | World-space bounds-center offsets and rocket flight direction |
| P2 | Filter cockpit candidates by visibility and range before ranking | Distant centered enemy, closer off-center enemy, wall obstruction, range boundary, dead/hidden/notarget and lock loss |
| P3 | Correct the inherited Goob/Napalm ready state | Empty and available ammunition, both firing modes and clip models; SP/MP Napalm initialization kept aligned |

`idAnimator` now keeps a separate temporary playback scale. Updating it adjusts
all active blends by the ratio between old and new scales; new animations combine
it with the existing base and authored rates. Frozen AI skip their decision and
movement state machines while physics and presentation continue. Status timers
update before the dormancy check.

Retch effects use an owner reference and independent damage multipliers. Ice has
visual priority over a buff; removing either reveals the remaining effect or
the actor's original overlay. The provider's death/removal clears only its own
buff. Restoration of provider effects is deferred until all target entities have
restored, avoiding object-order dependence.

Cockpit guides, visibility traces and homing use world-space target bounds center
plus `projectileTargetOffset`. Local content defines offsets on the harvester
and bomber, and the local alpha behavior reference confirms that coordinate
convention. No reference implementation was copied. An out-of-range guide is
shown only when no visible in-range candidate is available.

## Save format

The [format policy](savegame-compatibility-policy.md) defines v4 and the bounded
v3 decoder. Existing raw structures and footer/integrity versions stay unchanged.
Both game libraries read the v3 packed fields under v3 and v4. Old solid ice is
recoverable from its saved overlay but its original timer was never written, so
it receives four seconds after restore. Old partial chill safely thaws.

## Validation

- `tools/tests/awakening_status_behavior.py` compiles production methods against
  test doubles and checks outcomes. It does not launch the game or use assets.
- Existing Awakening gameplay, speeder, CI, save corruption, pointer-width and
  save format contracts remain required.
- `tools/tests/awakening_status_runtime.py` creates and loads a frozen AI save
  with the staged SP executable and user-supplied assets. A v3 fixture can be
  recorded before upgrading with `--legacy-writer`.
- `aiDumpState <entity> -v` reports freeze timers, playback rate, buff owner,
  damage scales and active overlay for diagnosis.

Build and runtime evidence for this task belongs under
`.tmp/awakening-fixes/`. Runtime runs are hidden and windowed, use independent
writable profiles, and use engine commands without mouse/keyboard control.
Qualification results are added here after the staged checks finish.

## Remaining content questions

The alpha's missing assets, empty race navigation and unfinished script/voice
content remain separate issues. Goob damage-over-time bypasses hit-region and
Retch resistance scaling through the existing raw damage path; changing that
needs a deliberate balance decision.

The freeze gun's battery recharges (17 November 2026, by user decision). The
alpha DLL only reads `ammoRegenStep`/`ammoRegenTime` under the ammo-regeneration
powerup, so a drained gun stayed empty; openQ4 now applies them without the
powerup: 5 every 200 ms up to the starting charge of 35, held or holstered.
Each shot holds the next pulse back by one second, so a sustained spray drains
it (80 to 0 in about 11 s) and pickups still stack past 35. The headless M04
probe recorded 0 to 35 within 4.5 s of switching away and back.

The coolant tubes show the charge. None of the alpha's freeze-gun materials
read a shader parm, so `materials/00_awakening_freezegun_openq4.mtr` in pak0
redefines `models/weapons/freezegun/freezegun_tube` ahead of
`q4x_weapons.mtr`, the same three glow stages scaled by parm 6. The weapon
writes parm 6 on the view model as the drained fraction of the 35 charge (0
full, so any other entity on that mesh keeps the full glow). A fixed-tic M04
capture showed bright tubes at 80, nearly dark tubes at 4 and bright again
after recharging to 35.
