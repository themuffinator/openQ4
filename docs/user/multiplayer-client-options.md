# Multiplayer Client Options

> This guide matches openQ4 0.15.0. Anything new in 0.15.0 is marked **New in 0.15.0**. See [release status](release-status.md) for what each download includes.

> Part of the [multiplayer guide](multiplayer.md), which covers joining, game types and match rules and lists every multiplayer guide.

**New in 0.15.0.** These options change what you see, hear and say in a
multiplayer match, and two of them let a server decide what its players may
show each other. None of them changes the rules of the game. Each has its own
setting, and each works in the Remastered, Minimal, Competitive and Classic HUD
layouts unless it says otherwise.

Set the HUD options and how you spectate in the menus, on **Multiplayer →
Player Setup → HUD**, or in a match on the in-match menu's **Settings → HUD**:
**Hit Sound**, **Crosshair Names**, **Crosshair Ammo**, **Item Timers**,
**Team Locations**, **Local Clock**, **Spectator Speed** and **Follow View**.
Your weapon order is on **Player Setup → Player → Weapon Order** and the
in-match **Settings → Player**. Everything else here, and the same settings,
can be typed in the console (`~`) or put in your `autoexec.cfg`. They are
saved with your profile.

## Chat tokens

Write a token in a chat line and it is replaced with your own status as the
line is sent. Everyone reads the result, whatever version they run.

| Token | Becomes | Example |
| --- | --- | --- |
| `#h` | Your health | `87` |
| `#a` | Your armour | `50` |
| `#w` | Your weapon and its ammunition | `Railgun (12)` |
| `#l` | Where you are | `Quad Damage` |
| `#n` | Team mates near you | `Ada, Bo` |

`say_team need help at #l, #h/#a` sends "need help at Quad Damage, 87/50".

- Tokens work in `say` and `say_team`, in the chat box and in a key binding:
  `bind f "say_team #w at #l"`.
- Type `##` for a plain `#`. A `#` that is not one of the five tokens, or that
  starts a longer word such as `#help`, is left as you typed it.
- A token with nothing to report becomes `-`. When you are dead, `#h` says so.
  A spectator's line is sent unchanged.
- `#n` names up to three living team mates within sight and about 20 metres.
  With none it says "nobody".

### Where "where you are" comes from

Quake 4's multiplayer maps have no named areas, so a place is named after the
nearest landmark on the map: a weapon, a powerup, mega health, armour or a
flag. A custom map that places `info_location` entities is named by them
instead. `listLocations` in the console prints a map's landmarks and the name
of the spot you are standing on.

## Hit sounds by damage

`hud_hitSound` chooses the sound you hear when you hit an opponent.

| Value | Sound |
| --- | --- |
| `0` | None |
| `1` | Quake 4's hit sound (default) |
| `2` | Four tones that fall as the damage grows: the lowest tone is the hardest hit |
| `3` | Four tones that rise as the damage grows |

The four bands are 1 to 25 damage, 26 to 50, 51 to 75 and anything above. The
tones are Quake 4's own hit sound at four pitches, and a hit on armour keeps
its own armour sound in every set.

The tones need the server to report how much each hit was worth, which is the
default (`g_hitFeedback 2`). On a server that withholds amounts, and for the
rare hit whose report is lost on the way, you hear Quake 4's sound instead.
Hits on team mates and on yourself stay silent.

## Weapon preference order

`ui_weaponOrder` lists the weapons you would rather hold, best first. In the
menus, **Weapon Order** opens a list of the weapons by rank: turn on **Use
Weapon Order**, pick a weapon, then pick the place it goes (pick it again to
put it back), or press **Reset** for the default order. Turning it off leaves
the setting blank. In the console:

```
seta ui_weaponOrder "rg rl lg hb ng gl sg mg"
```

- **Picking up a weapon** switches to it only when you rank it above the one in
  your hands. A weapon you did not list is never switched to. This needs
  `ui_autoSwitch` on. With `ui_autoSwitch 2` (Smart, the default) the pickup
  must also be one Smart allows: it takes over only from the gauntlet, the
  machinegun or an empty weapon. Set `ui_autoSwitch 1` to let your order alone
  decide. See [Weapon switching](gameplay-settings.md#weapon-switching).
- **Running out of ammunition** switches to the best ranked weapon you carry
  that still has some. When none of your listed weapons qualifies, Quake 4
  picks as it always has.

Name a weapon by its short name (`railgun`), its definition (`weapon_railgun`),
its slot number or one of these: `rg` railgun, `rl` rocket launcher, `lg`
lightning gun, `gl` grenade launcher, `hb` hyperblaster, `ng` nailgun, `nap`
napalm gun, `sg` shotgun, `mg` machine gun, `bfg` dark matter gun, `g`
gauntlet, `bl` blaster. Separate them with spaces or commas. Leave the setting
empty to leave the choice to `ui_autoSwitch` alone.

The server makes both choices, so the order works on any openQ4 server from
this version on. Buying always switches to what you bought.

## Crosshair

| Setting | Default | What it does |
| --- | --- | --- |
| `hud_crosshairAmmo` | `0` | `1` shows the ammunition of the weapon in hand just right of the crosshair. It turns red when the weapon is low. |
| `hud_crosshairNames` | `1` | Names the player under your crosshair: `0` nobody, `1` everyone, `2` team mates only. A team mate also shows health and armour. |

## Team list locations

In team modes the Competitive layout lists your team with each player's weapon,
health and armour. Each row now also names where that player is, and the list
stays accurate for team mates on the far side of the map: the server sends
your own team's positions and vitals to you twice a second. Opponents are
never sent. `hud_teamLocations 0` hides the names.

## Item timers

A server chooses who may see when the powerups, mega health and armour come
back, with `si_itemTimers`:

| Value | Who sees the timers |
| --- | --- |
| `0` | Nobody |
| `1` | Spectators only (default) |
| `2` | Everyone, players included |

When you are allowed to see them, up to six timers appear at the right edge of
the screen, the items a match turns on first. An item that is there shows its
name in green; one that is gone shows the time until it returns. A timer shows
`--` while a Classic mega health is still held by the player who took it.

`hud_itemTimers 0` hides the timers on your screen. The server sends the list
only to the people it allows, so a player on a spectators-only server has
nothing to reveal. Managed competitive matches keep their own rules about
timers; see [Competitive Matches](competitive-matches.md).

## Time of day

`hud_localClock 1` shows your computer's time at the right edge of the screen
in 24-hour form, `2` in 12-hour form, `0` (the default) hides it. The match
clock is unchanged.

## What a server may restrict

Brightskins, rimlights, outlines and forced models change how other players
look on your screen only. A server can limit them for everyone who plays on
it, with **Highlight Limit** and **Allow Forced Models** on **Create Server →
Rules** or these settings:

| Server setting | Default | Effect |
| --- | --- | --- |
| `si_maxBrightskin` | `1` | The strongest brightskin, rimlight and outline a player may put on other players, from `0` (none) to `1` (no limit). |
| `si_forcedModels` | `1` | `0` draws every player with the model they chose: `g_forceModel`, `g_forceMarineModel` and `g_forceStroggModel` are ignored. |

Your own settings are kept and apply again on a server without the limit.
Spectators are not limited. These limits are applied by each player's game, on
the server's instruction; the server does not send different data to different
players for them.

## For server operators

| Setting | Default | Notes |
| --- | --- | --- |
| `si_itemTimers` | `1` | Takes effect at once. Send `rescanSI` so the browser shows it. |
| `si_maxBrightskin` | `1` | Send `rescanSI` after changing it on a running server. |
| `si_forcedModels` | `1` | Send `rescanSI` after changing it on a running server. |
| `g_hitFeedback` | `2` | `2` lets players use hit tones and damage numbers; `1` confirms hits without amounts; `0` sends no hit feedback. |

See [Server Setup](server-setup.md) for where server settings live.
