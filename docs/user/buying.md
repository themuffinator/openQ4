# Buying in multiplayer

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

**Not in 0.14.1.** Some servers turn off the weapon, armour and ammunition
pickups and let you buy your gear instead, with credits you earn in the match.
A server sells by one of two sets of rules: **Quake 4**'s, the buy menu the
retail game shipped, or **The Awakening**'s, the expansion's unfinished buy
menu completed, with its prices, weapon mods and an extra team powerup. You do
not need The Awakening installed to play by its rules.

Buying works in Deathmatch, Team DM, the four Capture the Flag modes and
DeadZone. Tourney, Duel and the round modes (Clan Arena, Freeze Tag, Red Rover)
are played with a fixed loadout and have no buy menu.

## Opening the buy menu

Press your **Buy Menu** key (**B** by default; see
[Input Settings](input-settings.md)) once the match is live. You cannot buy
during the warmup or while you spectate. In the new interface the buy menu is a
card over the match, like the in-match menu; in the classic interface it is
Quake 4's own buy screen.

The card's header shows the rules this server sells by and your credits. Its
four tabs hold **Weapons**, weapon **Mods**, **Armor & Ammo** and **Team**
powerups; switch them with **Q** and **E** (the shoulder buttons on a
controller). Each item shows its price:

- A price in yellow is one you can pay. **Enter** (or your controller's accept
  button) buys it.
- A **lock** means you cannot buy it now. The panel beside the list says why:
  not enough credits (the price turns red too), team modes only, carry the
  weapon first, or already full.
- A **check** means you already have it.

The panel also shows the item, its price and what it does. The menu stays open
after a purchase, so you can buy several things in a row; **Esc** returns to
the match.

## What each set of rules sells

Team powerups last 30 seconds for your whole team and are sold in Team DM, CTF
and DeadZone.

| Item | Quake 4 | The Awakening |
|---|---|---|
| Shotgun | 600 | 500 |
| Hyperblaster | 1500 | 700 |
| Grenade Launcher | 700 | 1000 |
| Nailgun | 1200 | 900 |
| Rocket Launcher | 2750 | 2000 |
| Railgun | 2750 | 3000 |
| Lightning Gun | 2000 | 1500 |
| Dark Matter Gun | not sold | 5000 |
| Napalm Gun | 3500 | not sold |
| Bounce Shot Mod (hyperblaster) | not sold | 8000 |
| Firepower Mod (nailgun) | not sold | 5000 |
| Burst Fire Mod (rocket launcher) | not sold | 12000 |
| Chain Lightning Mod (lightning gun) | not sold | 6000 |
| Light Armor (+50) | 300 | 300 |
| Heavy Armor (+100) | 600 | 700 |
| Ammo Refill | 200 | 500 |
| Team Ammo Regen | 1000 | 10000 |
| Team Health Regen | 2000 | 12500 |
| Team Doubler (almost double damage) | 3000 | 15000 |
| FC Armor Regen (armour builds past its maximum) | not sold | 3000 |

Quake 4's prices come from the game's own data, so a mod that changes them
changes these too.

### Weapon mods

The Awakening sells a mod for the weapon you carry; buy the weapon first. A
weapon keeps its mod for the rest of the match, and the game swaps it out and
back in when the mod arrives.

- **Bounce Shot Mod**: hyperblaster bolts bounce off walls.
- **Firepower Mod**: the nailgun fires a third faster.
- **Burst Fire Mod**: the rocket launcher fires three homing rockets in quick
  succession, then reloads.
- **Chain Lightning Mod**: the lightning gun arcs on to enemies near your
  target.

The expansion's shotgun and railgun mods are not sold. The multiplayer shotgun
has no clip to extend, and the multiplayer railgun already passes through
players at full damage. Its concussion grenades and its core cannon, fire
cannon and freeze gun need the expansion's own files, which multiplayer does
not load.

## Earning credits

You start each match with 1,000 credits and can hold up to 25,000. Both rules
pay for kills, flag captures, returns and steals and for killing the enemy flag
carrier, and charge 300 credits for a suicide. They differ in the amounts:

| Event | Quake 4 | The Awakening |
|---|---|---|
| Killing an opponent | 600, scaled to the frag limit in DM and Team DM | 500, scaled the same way |
| Killing a teammate | costs 400 | free |
| Capturing the flag | 600, plus 400 for everyone on your team | 1000, plus 1000 for everyone on your team |
| Returning your flag / stealing theirs | 500 / 300 | 500 / 200 |
| Killing the flag carrier | 500 | 500 |
| Winning or losing a DeadZone round | nothing | 3000 for the winners, 2000 for the losers |

Medals and DeadZone control pay the same under both rules.

## What you keep

Weapons you buy, and their mods, come back when you respawn, with fresh
ammunition. Armour and a refill bought while you are dead are waiting when you
respawn. Team powerups restart on a teammate who respawns while they run.

## Hosting a buying server

In **Multiplayer → Create Server → Advanced Server Options → Rules**, set
**Buying** to **Off**, **Quake 4** or **The Awakening**. The in-match menu's
**Vote** and **Admin** pages offer the same choice, and changing it restarts
the map. From the console:

| Setting | What it does |
|---|---|
| `si_isBuyingEnabled` | `1` turns buying on |
| `si_buyingRules` | `0` sells by Quake 4's rules, `1` by The Awakening's |
| `si_buyModeStartingCredits` | Credits each player starts with (default 1000) |
| `si_buyModeMaxCredits` | Most credits a player can hold (default 25000) |
| `si_dropWeaponsInBuyingModes` | `1` makes dead players drop their weapons anyway |

`callvote buying 2` asks for a vote to sell by The Awakening's rules (`0` turns
buying off, `1` sells by Quake 4's). Everyone on the server must run the same
openQ4 version; see [Server Setup](server-setup.md).

The classic interface's buy screen lists Quake 4's items. On a server using
The Awakening's rules it shows The Awakening's prices, but the mods, the Dark
Matter Gun and FC Armor Regen are bought from the new interface's card, or with
the console command `buy`, for example `buy wpmod_hyperblaster_bounce1`.
