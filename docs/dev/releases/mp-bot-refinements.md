# Multiplayer bot refinements — pending release notes

> **Detailed change record.** Everything recorded here up to 11 November 2026 shipped in [0.14.1](v0.14.1.md), whose notes are the player-facing summary. Later additions are unreleased development notes. See [release status](../../user/release-status.md).

## Highlights

- **Bots stop knocking team-mates off walkways.** Before firing a rocket or
  other blast weapon, a bot checks where the blast would push each nearby
  team-mate, with team damage on or off, and holds fire when the push would
  throw one into the void.
- **Bots no longer rail through an enemy into a team-mate.** The multiplayer
  railgun passes through players, so a bot now holds fire when a team-mate
  stands anywhere along the shot, behind its target included.
- **Bots swim down to underwater routes.** In maps with water, a bot whose
  route leads to a pool floor dives for it while it has air to spare, instead
  of treading water at the surface until it gives up.
- **Bots jump across shallow trenches.** Where a trench is shallow enough to
  drop into, bots also learn the running jump across it to the far side, when
  the whole jump is clear. Most stock maps gain a few of these routes.
- **Bot route drawing stays live on a listen server.** `bot_debugNav` no longer
  fills up and freezes after a few seconds on the host's own screen.
- **Bots find more recoverable landings after a hit.** Air steering checks finer
  directions and keeps a safe heading. A critically hurt bot with no safe impact
  can aim for a reachable health pickup instead of braking into the void.
- **Scripted bot replacement works reliably.** Removing a bot frees its slot
  before the next buffered command tries to add the same character.
- **Bots fire more sensibly around cover.** A rocket into the wall beside you is
  no longer "useful" when you are behind cover, and a bot behind a pillar no
  longer refuses a safe shot because it believed its own blast would reach it
  through the pillar. Shots across water fly as the real projectile does, and
  a quad-carrying bot prices its own splash at the damage quad actually does.
- **Bots get out of napalm and away from grenades.** A napalm glob burning on
  the floor is treated as the hazard it is, and the step away from an explosive
  lying still goes straight away from it rather than sideways into it.
- **Bots keep their eyes up and carry the right gun.** Between fights a bot
  watches the route ahead at eye height instead of staring at the floor in
  front of its feet, a bot standing watch sweeps the room, and the weapon it
  carries across the map is the one its personality fights with.
- **Low-skill bots stay low-skill.** A character's eagerness can no longer
  shorten its reaction to skill-4 speed or remove its aim settle entirely, so
  skill 1 and 2 opponents read as the beatable players they are meant to be.
- **Team orders and roles play fairer.** "Everyone attack" in Freeze Tag no
  longer stops every rescue for the rest of the map, a bot told to follow a
  dead leader rejoins the team's plan instead of standing idle, and Dead Zone
  bots (and the HUD) always see one zone holder.
- **Navigation is tidier and cheaper.** Shortcuts no longer walk a bot through a
  sunken floor or across pain floor the route planner priced to avoid, items
  beside a diagonal jump pad can be picked up, a bot pressed against a lip
  during a long-range duel frees itself, and route searches, item snaps and
  recovery hops cost the server less.
- **Bots no longer wall-hug under a platform or bounce on a vertical pad.** A
  shortcut can no longer aim a bot at a corner on top of a riser or a ramp's
  side it cannot climb (q4ctf1's platform and ramp), and a vertical jump pad
  whose ledge needs a little air control is flown onto the ledge instead of up
  and down its shaft (q4dz1's pads).
- **Bots keep off the edge, and recover when knocked off a route.** A route
  no longer runs along the last few units of a platform or catwalk with the
  bot's feet hanging over the drop (q4ctf1's platforms, q4dz1's catwalk), and
  a bot strafed or knocked off the side of a ramp mid-fight re-routes from
  where it lands instead of pressing at a corner it can no longer reach.
- **Bots ride out knockback onto a walkway, and take their planned drops.** A
  bot knocked off the edge of a walkway now steers back onto it when its
  momentum carries it there, and a dodge whose landing turns unsafe in flight
  hands over to air recovery instead of finishing the jump. Bots no longer stall
  at the top of a drop their route plans (q4ctf5), and a bot chasing a moving
  flag carrier keeps chasing while its new route makes progress.

## Upgrade Notes

- Replace the complete package so the engine, renderer modules and both game
  modules remain matched. Original Quake 4 assets are still required.
- No save format or network protocol change. Server operators need not change
  any `bot_*` setting; `bot_skillVariance` now spreads skill either side of
  `bot_skill` evenly instead of pinning part of the roster to the end of the
  range.

## Change Log

- Splash safety, usefulness and threat cover tests no longer pass through the
  world or a mover; the speculative projectile sweep ignores water; splash and
  direct damage carry the shooter's damage powerups; a direct hit is priced at
  its direct damage alone; with team damage off an impact beside a team-mate is
  still refused for the knockback.
- Napalm globs and other lingering rounds count their emitted-damage radius and
  are dodged once at rest; dodges from a resting threat move away from it.
- The view rests 320 units along the route at eye height, and a bot standing
  watch sweeps 55 degrees either side of its facing; the carried weapon is
  chosen for the personality's combat range when no enemy is known.
- Combat footwork silences the stuck detector only when it bends the route by
  more than about 30 degrees.
- `initiative` scales reaction by a quarter at most and never removes more than
  three quarters of the aim settle; `itemFocus` weighs a pickup against an
  enemy; `aimLead` and `aimLeadError` clamp at 1; `pursuit` no longer
  over-extrapolates a lost target's velocity; a queued chat line is not
  replaced by a newer event; `bot_skillVariance` reflects off the range ends.
- Team orders that resolve to nothing leave the bot in role allocation; Dead
  Zone ownership reads one holder, and the game clears both slots when the zone
  changes hands.
- String-pulled shortcuts refuse ground that climbs more than a step between
  floor samples unless the ground between them climbs at most a step on either
  side, and a shortcut that climbs or descends is proven from either side of
  its corner. A pad's natural landing is the nearest node with floor under the
  middle of the hull, proven from the pad's centre ridden or steered as the
  bot flies it.
- Navigation nodes whose hull hangs more than eight units past an edge are
  rim nodes: routes pay a cell more to step onto them, so they run a cell
  inland wherever there is an inland, and a shortcut needs the ground beside
  it as well as under it. A bot more than a corner's reach off a walk leg, or
  a step off its height, re-routes as soon as the walk from where it stands
  to its corner no longer holds. `navmesh probe x y z` reports the nodes,
  links and floor around a point.
- String-pulled shortcuts refuse pits deeper than a step and survivable pain
  floor; route goals beside a transport are judged against its brush; stale
  edge validation sweeps only recorded movers; the cell hash mixes both grid
  coordinates; failed entity snaps are retried after five seconds; partial
  step lifts under low ceilings are accepted; recovery hops are rate limited
  and skip transport sweeps when none is in reach.
- Chat banks drop the bare `medic` trigger so a medic character's own greeting
  no longer draws help replies from the roster.
