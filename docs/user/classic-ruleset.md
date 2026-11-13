# Classic Multiplayer Ruleset

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

**Not in 0.14.1.** The Classic ruleset turns Quake 4 multiplayer into something
that plays like Quake 2 deathmatch and Threewave CTF, on the stock Quake 4 maps
and weapons. It is strongly inspired by Quake 2 rather than a copy of it: the
numbers lean on Quake 2's, and Quake 4's models, effects and maps stay as they
are.

Every game type works under it. The default, **Quake 4**, plays exactly as
before.

## Turning it on

Hosting from the menus: **Multiplayer → Create Server → Advanced Options →
Rules**, then set **Ruleset** to **Classic**. **Grappling Hook** on the same tab
chooses where the hook is allowed: **Off**, **Flag Modes** (the default, as in
Threewave CTF) or **All Modes**.

From the console or a server config:

| Setting | Values |
| --- | --- |
| `si_ruleset` | `0` Quake 4 (default), `1` Classic |
| `si_classicGrapple` | `0` off, `1` flag modes only (default), `2` every mode |

Changing `si_ruleset` while a map is running restarts the map, so everyone
always plays by the same rules. Players joining a Classic server need nothing
extra; they pick the rules up from the server.

## What changes

**You spawn with the blaster and 100 health**, nothing else. The blaster takes
the gauntlet's slot (key **1** by default) and fires a 15-damage bolt twice a
second for as long as you hold fire. It never runs out.

### Weapons

Each Quake 4 weapon plays its Quake 2 counterpart.

| Weapon | Plays as | Damage | Fire rate | Ammo |
| --- | --- | --- | --- | --- |
| Blaster | Blaster | 15 bolt | 2 a second | none needed |
| Machinegun | Machinegun | 8 | 10 a second, wider spray | bullets |
| Shotgun | Super Shotgun | 20 pellets of 6 | 1 a second | 2 shells a shot |
| Hyperblaster | Hyperblaster | 15 bolt, no splash | 10 a second | cells |
| Grenade Launcher | Grenade Launcher | 120 over 160 units, 2.5 s fuse | 1 a second | grenades |
| Nailgun | Chaingun | 10 a nail | 12 a second | bullets |
| Rocket Launcher | Rocket Launcher | 110 hit, 120 over 120 units, slower rockets | 1 every 0.8 s | rockets |
| Railgun | Railgun | 100, through everyone in line | 1 every 1.5 s | slugs |
| Lightning Gun | Lightning Gun (no Quake 2 twin) | unchanged | unchanged | cells |
| Dark Matter Gun | BFG10K | 200, over a very wide area | 1 every 2.5 s | 50 cells a shot |

The machinegun and nailgun share **bullets**; the hyperblaster, lightning gun
and Dark Matter Gun share **cells**. A weapon pickup gives one box of its ammo
on top of what you carry.

### Ammo

| Ammo | Box | Most you can carry |
| --- | --- | --- |
| Bullets | 50 | 200 |
| Shells | 10 | 100 |
| Cells | 50 | 200 |
| Grenades | 5 | 50 |
| Rockets | 5 | 50 |
| Slugs | 10 | 50 |

The nailgun, lightning gun and Dark Matter Gun ammo boxes give bullets or cells.

### Armour comes in tiers

| Pickup | Gives | Holds up to | Stops normal damage | Stops energy damage |
| --- | --- | --- | --- | --- |
| Armour shard | +2 to what you wear, or 2 jacket armour if you have none | 250, past the tier's limit | as worn | as worn |
| Jacket armour (green) | 25 | 50 | 30% | none |
| Small armour, plays combat armour (yellow) | 50 | 100 | 60% | 30% |
| Large armour, plays body armour (red) | 100 | 200 | 80% | 60% |

- A **better** armour replaces the one you wear and keeps what is left of the
  old one at its worth: 51 combat armour picked up into body armour becomes
  138 body armour.
- A **weaker** armour tops your current armour up by its worth, and you cannot
  pick it up if that adds nothing.
- Armour can stop a hit completely, and it never drains away.
- **Energy** weapons (blaster, hyperblaster, lightning gun, Dark Matter Gun)
  get through armour more easily.

### Health

| Pickup | Gives |
| --- | --- |
| Health shard, plays the stimpack | +2, even past 100 |
| Small health | +10, up to 100 |
| Large health | +25, up to 100 |
| Mega health | +100 past 100 (up to 250) |

Health over 100 from shards stays. **Mega health** holds its extra points for
five seconds, then drains one point a second until you are back to 100. The
mega health item only starts its 20-second respawn once its holder is back to
100, dies or leaves.

Items come back like Quake 2's: weapons, ammo and health after 30 seconds,
armour after 20, quad damage after a minute.

### Movement

Movement keeps Quake 4's speed and jump, with one Quake 2 trick: a **stair
double jump**. Jump again within 0.4 seconds of your last jump and the second
jump launches you higher. Flat ground is too slow for it, so it works where it
did in Quake 2: up stairs, onto ledges and off ramps.

## CTF

Captures, the flag and the bases work as in Quake 4, scored the Threewave CTF
way. These points go to your CTF score:

| Event | Points |
| --- | --- |
| Capturing the flag | 15, and 10 to each team mate |
| Fragging the enemy flag carrier | 2 |
| Fragging someone who just hurt your flag carrier | 2 |
| Fragging an enemy near your flag, or while you defend it | 1 |
| Fragging an enemy near your team's flag carrier | 1 |
| Returning your dropped flag | 1 |
| Returning the flag shortly before your team captures | +1 |
| Fragging the enemy carrier shortly before your team captures | +2 |
| Picking up the enemy flag | nothing |

## The grappling hook

Where the hook is allowed, hold the **Grappling Hook** key to fire it. It flies
straight from your eyes, bites into walls and ceilings, and reels you in while
you keep holding the key. Let go to drop off and keep your momentum. The cable
shows in your team's colour.

- It does not hurt anyone, and it lets go if it hits a player or the sky.
- It does not follow moving platforms.
- You can carry the flag while hooked.

Bind it under **Settings → Controls → Attack/Look → Grappling Hook**, or from
the console, for example `bind MOUSE4 _hook`. It has no default key.

## Limits

- Bots play Classic matches but never use the hook.
- Item names on the pickup notices are still Quake 4's ("Small Armor Vest").
- The armour counter does not show the tier's colour yet.
- With [Instagib](server-setup.md#instagib) on, its railgun and one-hit kills
  replace Classic's weapons and armour. The double jump and the hook still
  work.
