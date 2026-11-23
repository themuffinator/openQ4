# Multiplayer guide

> This guide matches openQ4 0.15.0. Anything new in 0.15.0 is marked **New in 0.15.0**. See [release status](release-status.md) for what each download includes.

> **Experimental.** Multiplayer is still under active development. Expect rough
> edges, bugs and behaviour that changes between versions.

This is the place to start for openQ4 multiplayer. It covers what you need
before you play, how to join or host a game, the game types and the rules that
change them, and what you can do during a match: talk, give bots orders,
spectate, vote and call timeouts. Where a topic has more to say, it links to a
detailed guide; [All multiplayer guides](#all-multiplayer-guides) lists them
by task.

## Before you play

- **Everyone needs openQ4, and the same version.** The server and every player
  need the same openQ4 version and the same game files. Different versions are
  not guaranteed to play together: 0.13.0 and 0.14.1, for example, refuse each
  other, with a pure server reporting missing packages and other servers
  reporting that data is out of sync. 0.15.0 also uses a newer network
  protocol (2.42, where 0.14.1 uses 2.41), so 0.15.0 and 0.14.1 leave each
  other out of the server list. See
  [Older openQ4 versions](multiplayer-networking.md#older-openq4-versions).
- **Retail Quake 4 cannot play with openQ4.** Retail Quake 4 1.4.2 clients and
  servers use a different network protocol (2.85), so neither side can join
  the other. openQ4 refuses them with an explanation instead of hanging; see
  [Retail Quake 4 servers](multiplayer-networking.md#retail-quake-4-servers).
- **Nothing else to install.** Bots, the Classic ruleset, modifiers and buying
  by The Awakening's rules all run on the server: a player needs only openQ4
  and their Quake 4 files. Mods built for the original Quake 4 game DLLs do
  not load.

## The Multiplayer menu

Choose **Multiplayer** on the main menu:

| Entry | What it is for |
|---|---|
| **Join Game** | Find servers on the internet or your local network, sort and filter them, then join or spectate. See [Find and join a server](multiplayer-networking.md#find-and-join-a-server). |
| **Create Server** | Host a match of your own: the map, game type, rules, modifiers and bots. See [Hosting from the game](server-setup.md#hosting-from-the-game). |
| **Demos** | Watch matches you recorded, following any player. See [Watch a demo](multiview-demos.md#watch-a-demo). |
| **Player Setup** | Your name, clan tag, player model and rail colour, your weapon order, your HUD options, and how opponents and team mates look to you. See [Your multiplayer settings](#your-multiplayer-settings). |

The classic menus (`ui_retained 0`) offer the same things in Quake 4's
original layout.

## Join a game

1. Open **Multiplayer → Join Game** and choose **Internet** or **LAN**.
2. Select a server to see its map, players and settings. A ping of up to
   60 ms shows four bars; fewer bars mean a slower connection.
3. Choose **Join Server** to play or, in the new menus, **Spectate** to join
   watching. A passworded server asks for its password.

To join a server you know, choose **Join IP Address** and type its address, or
open the console and enter `connect <address>`, for example
`connect 192.168.1.50`. Without a port, openQ4 uses `28004`. IPv6 addresses go
in square brackets when a port follows: `connect [2001:db8::1]:28004`. See
[Connect by address](multiplayer-networking.md#connect-by-address).

When you arrive, the new interface shows a welcome card that offers to join
the game and shows the server, its players and your settings. Press Escape at
any time during the match for [the in-match menu](#the-in-match-menu).

## Play against bots

You can play a full match on your own computer with no internet connection:

1. Open **Multiplayer → Create Server** and pick a map and **Gametype**.
2. On the **Match** tab, set **Fill With Bots** to the number of players you
   want and choose a **Bot Skill**, from Training to Impossible.
3. On the **Server** tab, set **Server Type** to **LAN**, then start the
   server.

Bots fill the free places and leave one by one as people join. Chat, quick
chat and voice chat work with bots too, and in team games you can
[give them orders](#give-bots-orders). For bot matches with a structure, unlocks
and saved progress, try the [Arena Campaign](arena-campaign.md).

## Host a game for friends

- **While you play:** open **Multiplayer → Create Server**, set **Server
  Type** to **Internet** (or **LAN** for players on your own network) and
  start the server. Friends find it in their server browser or join by your
  address, which **Join IP Address** shows.
- **On its own:** a dedicated server runs without a game window and keeps
  running when you stop playing. See
  [Running a dedicated server](server-setup.md#running-a-dedicated-server).

Players outside your network can reach you only once inbound UDP port `28004`
(or the port you chose) is open in your firewall and forwarded by your router;
see [Firewall and router](server-setup.md#firewall-and-router). The
[server setup guide](server-setup.md) covers every server setting, map
rotation and bots, and the [server security guide](server-security.md) covers
remote administration and pure servers.

## Game types

Choose the game type with **Gametype** on Create Server, or `si_gameType` on a
dedicated server.

| Game type | `si_gameType` | How it plays |
|---|---|---|
| Deathmatch | `DM` | Everyone for themselves. The most frags at the frag or time limit wins. |
| Team Deathmatch | `Team DM` | Marines against Strogg; every frag counts for your team. |
| Tourney | `Tourney` | A bracket of one-on-one fights, each in its own arena of the map. Winners go through to the next round until one player is left. |
| Duel | `Duel` | One against one, winner stays: everyone else waits in a queue for the next fight. See [Duel](#duel). |
| Capture the Flag | `CTF` | Take the enemy flag to your own base while your flag is at home. |
| One Flag Capture the Flag | `One Flag CTF` | One neutral flag; carry it into the enemy base to score. |
| Arena Capture the Flag | `Arena CTF` | Capture the Flag with Quake 4's runes: Scout, Guard, Doubler and Ammo Regen. |
| Arena One Flag Capture the Flag | `Arena One Flag CTF` | One Flag CTF with the runes. |
| DeadZone | `DeadZone` | Teams score control time while they hold the map's zone. |
| Clan Arena | `Clan Arena` | Rounds of team elimination. Everyone starts each round with every weapon, full ammunition and armour, and there are no pickups. |
| Freeze Tag | `Freeze Tag` | Rounds of team elimination where a kill freezes the victim and team mates can thaw them. Freeze the whole other team to win the round. |
| Red Rover | `Red Rover` | A player you kill joins your team. The round ends when one side is empty, and players are ranked on their own scores. |

Duel, Clan Arena, Freeze Tag and Red Rover come from Quake Live and play on
the stock Quake 4 maps. Overload, Harvester, Domination and Attack & Defend
are not finished, so openQ4 hides them from hosting, voting and the server
browser's filters. [Match rules](#match-rules) explains how matches start and
end in each mode.

## Change how a match plays

A server can change the rules of any game type. Players pick the rules up
from the server, and the match name in the server browser shows what is on.

- **Rulesets** (**New in 0.15.0**). The [Classic ruleset](classic-ruleset.md)
  plays like Quake 2 deathmatch and Threewave CTF, with Quake 2-style weapons,
  tiered armour, a stair double jump and a grappling hook.
- **Modifiers** (**New in 0.15.0**). [Instagib](server-setup.md#instagib),
  Quad Hog, Vampiric Damage, Weapons Frenzy and weapon arenas, plus damage,
  knockback and respawn settings. See
  [Modifiers and presets](server-setup.md#modifiers-and-presets).
- **Presets** (**New in 0.15.0**). A named bundle of game type, ruleset,
  modifiers and limits, such as `rocketarena` or `instactf`. See
  [Presets](server-setup.md#presets).
- **Buying** (**New in 0.15.0**). Pickups give way to a buy menu, by Quake 4's
  rules or The Awakening's. See [Buying in multiplayer](buying.md).

## During a match

### The in-match menu

Press Escape during a match. In the new interface the menu is a card over the
match with eight tabs; switch them with **Q** and **E**, or a controller's
shoulder buttons:

| Tab | What it holds |
|---|---|
| **Team** | Join or switch a team, spectate, or ready up. |
| **Players** | Everyone on the server with their scores, accuracy and medals; mute a player here. |
| **Vote** | Call a vote and cast yours; see [Vote](#vote). |
| **Match** | Match Control: ready-up, rosters, timeouts, proposals and series in a [managed match](competitive-matches.md#open-match-control). |
| **Settings** | Your player and HUD settings, without leaving the match. |
| **Voice** | Microphone access, volumes and a local microphone test; see [Voice chat](multiplayer-chat.md#voice-chat). |
| **Server** | The server's name, address, message, rules and map rotation. |
| **Admin** | Remote administration for those with the server's password; see [The in-match Admin page](server-security.md#the-in-match-admin-page). |

**Main Menu** and **Disconnect** are on the prompt bar at the bottom of the
card. The classic menus hold the same choices in Quake 4's layout.

### Talk

Press **Chat** to talk to everyone or **Team Chat** to talk to your team, hold
**Quick Chat** for a wheel of ready-made phrases, and hold **Voice Chat** to
speak once you have turned on microphone access. See
[Multiplayer chat](multiplayer-chat.md). Chat tokens such as `#h` and `#l`
put your health and location into a line (**New in 0.15.0**); see
[Chat tokens](multiplayer-client-options.md#chat-tokens).

### Give bots orders

In a team game you can tell the bots on your side what to do in team chat.
Start with a bot's name, or `everyone` for all of them, and follow it with the
order:

| Type | The bot |
| --- | --- |
| `kane follow me` | stays with you and fights beside you |
| `everyone defend` | holds your team's base in Capture the Flag |
| `sorg hold here` | holds the area where you are standing |
| `kane attack` | goes for the enemy flag, or escorts whoever has it |
| `everyone dismissed` | goes back to playing on its own |

`escort me`, `cover me`, `guard`, `get the flag` and `free` work too, and you
can name several bots at once: `sorg and bagby defend`. The bots answer in team
chat so you know the order landed. An order lasts until you give another one or
dismiss them, and it ends if the bot changes team or the map changes. A bot
carrying the flag still takes it home, and a badly hurt bot still grabs health
on the way.

Bots also chat: they answer common phrases, and addressing a bot by name makes
it the one that replies. Server operators choose how many bots play and how
well; see [Bots](server-setup.md#bots).

### Spectate

Choose **Spectate** in the server browser or on the in-match **Team** tab.
While you spectate, the **Status** section of the in-match **Match** tab has
**Previous**, **Next** and **Free Camera** buttons, or use these console
commands:

| Command | What it does |
| --- | --- |
| `follow next` or `follownext` | Follow the next player you are allowed to watch. |
| `follow prev` or `followprev` | Follow the previous player. |
| `follow free` or `followfree` | Fly freely, if the server allows it. |
| `follow <client-slot>` | Follow the player now in that numbered slot. |
| `follow <name>` | Follow a player by name. Part of a name is enough when only one player matches; colours are ignored. |
| `followlist` | List the players you can follow, with slot, name, team and score. |
| `follow auto <mode>` or `followauto <mode>` | Let the server move the camera for you (see below). |
| `followview <first\|third\|chase\|next>` | Choose how you see the player you follow. |

On a casual server a spectator is neutral and can follow anyone on either
team. A player who is out for the round (eliminated, or waiting between
Tourney fights) is not a spectator: they can follow their own team mates, and
they see what their team sees and no more. Bots can be followed. A request to
follow one player never ends up following someone else who has since taken
their slot. A managed match limits who may follow whom; see
[Spectate a managed match](competitive-matches.md#spectate-a-managed-match).
Demo playback has its own camera controls.

#### Auto-follow

`follow auto <mode>` hands the camera to the server, which moves it between the
players you are allowed to watch:

| Mode | The camera follows |
| --- | --- |
| `killer` | Whoever kills the player you are watching, a moment after the kill. |
| `action` | The most interesting player: a flag carrier first, then powerups, recent kills and the lead. |
| `leader` | The player with the top score. |
| `powerup` | The player carrying the most powerups; it stays put while nobody has one. |
| `flag` | A flag carrier; it stays put while the flags are home. |
| `off` | Nobody: you choose again. |

`follow auto` on its own means `action`. The camera holds each player for at
least three seconds so it does not flicker. Choosing a player yourself, or
pressing attack or jump, turns auto-follow off; the server tells you when it
does. In Tourney the camera stays in the arena you are watching.

#### Follow views

`followview` changes only how your own screen shows the followed player. The
setting is `g_spectatorFollowView` and is remembered; **Follow View** on
**Player Setup → HUD** sets it too.

| View | What you see |
| --- | --- |
| `first` | Through the player's eyes, with their weapon and crosshair. This is the default. |
| `third` | From behind the player, turning as they turn. |
| `chase` | A camera that circles the player as you move your mouse. |

`g_chaseCameraRange` (32 to 256, default 96) and `g_chaseCameraHeight` (-16 to
96, default 12) set how far back and how high the third-person and chase
cameras sit. The camera pulls in when a wall is in the way.

Set `g_deathCamera 1` to watch your own death from the same camera, facing
whoever killed you, instead of the tilted first-person view.

#### Free camera speed

`ui_spectatorSpeed` (**Spectator Speed** on **Player Setup → HUD**) sets how
fast you fly as a percentage of the server's speed, from 25 to 300 (default
100). For example, `ui_spectatorSpeed 50` is half speed for slow tracking
shots. It takes effect within a few seconds of changing it.

#### Spectator chat

While a match is live, what a spectator says in all-chat reaches only other
spectators unless the server sets `g_spectatorChat 1`. The server tells you
when your line was kept from the players. Team chat from a spectator always
goes to spectators only.

#### Cast a match

A server can let spectators see both teams' health and armour with
`caster on`, and openQ4 can write a match's events to a file for broadcast
overlays. See [Cast a casual match](competitive-matches.md#cast-a-casual-match)
and [Feed a broadcast overlay](competitive-matches.md#feed-a-broadcast-overlay).

### Vote

On a server that allows voting (`si_allowVoting 1`), only active human players
call and cast votes; bots do not, and they do not raise the number needed to
pass. Who can vote is fixed until the vote ends, even if players disconnect.

**New in 0.15.0.** Call a vote with `callvote` (or `cv`) and cast yours with
`vote yes` or `vote no`, or from the in-match menu's **Vote** page, which
offers every vote below but `scorelimit`: the map, **Skip to Next Map** and
**Random Map**, the game type and its limits, the **Overtime Period**, the
**Ruleset**, **Instagib**, buying, balance and shuffle, **Ready Everyone**,
a **Coin Toss** and kicking a player. After 0.15.0 the page also offers
**Quad Hog** (in Deathmatch), **Vampiric Damage**, **Weapons Frenzy**, a
**Weapon Arena**, a **Preset**, **Spectator Chat** and **Mute Instead of
Kicking** for the player chosen. The old `clientCallVote` still works.
The votes are:

| Vote | What passes |
|---|---|
| `map <name>`, `nextmap`, `random` | Change to that map, skip to the next map in the cycle, or draw a random installed map that supports the game type |
| `gametype <type>`, `ruleset q4\|classic`, `instagib 0\|1` | Change the game type, play the [Classic ruleset](classic-ruleset.md) or Quake 4's, or turn [Instagib](server-setup.md#instagib) on or off; each restarts the match |
| `timelimit <min>`, `fraglimit <n>`, `capturelimit <n>`, `scorelimit <n>`, `roundlimit <n>`, `overtime <sec>` | Change a limit; `scorelimit` is the limit the current game type scores by, and `overtime 0` uses sudden death |
| `shuffle`, `balance 0\|1`, `autobalance 0\|1` | Shuffle the teams, or turn automatic balancing on or off |
| `readyall` | Mark everyone ready, during a warmup that waits for ready players |
| `kick <name>`, `restart`, `buying 0\|1\|2`, `controlTime <sec>` | Kick a player, restart the map, change the buying rules, change DeadZone's control time |
| `cointoss` | Nothing changes; the server announces heads or tails for everyone |
| `mute <name>` | Stop that player's text chat reaching anyone, for ten minutes unless the server sets another time. **After 0.15.0**; players on 0.15.0 do not see the vote |
| `modifier <name> 0\|1`, or the modifier's name: `quadhog 1`, `vampiric 1`, `frenzy 1` | Turn a [modifier](server-setup.md#modifiers-and-presets) on or off; Quad Hog only in Deathmatch. The match restarts. **After 0.15.0** |
| `arena <weapon>`, `arena none` | Play a single-weapon arena (`rocketlauncher`, `railgun` and the rest), or none. The match restarts. **After 0.15.0** |
| `preset <name>` | Apply a [preset](server-setup.md#modifiers-and-presets), built in or one of the server's own; it sets the game type, ruleset, modifiers and limits together, so it is a vote of its own. **After 0.15.0** |
| `spectatorchat 0\|1` | Let spectators' all-chat reach the players during live play, or keep it to spectators. **After 0.15.0** |

The operator chooses the rules of voting: `g_votePercent` (the share of the
eligible players that must say yes, 51 by default, so a plain majority),
`g_voteLimit` (how many votes one player may call on a map, unlimited by
default), `si_spectatorVote` (whether spectators may vote, off by default)
and `si_voteFlags` (which votes are forbidden). A vote fails as soon as the
players who have not voted could no longer carry it. Each player waits thirty
seconds after their vote ends before calling another.

A spectator, for voting, is someone who chose to watch. A player who has been
eliminated for the round, or who is looking at the end-of-match scoreboard,
is still a player and can call and cast votes.

A limit changed by a vote applies to the match being played where the game
can apply it at once, and stays in force for the maps that follow. An
`overtime` or `timelimit` vote passed during a live match takes full effect
from the next match.

A managed match replaces these votes with proposals; see
[Vote on proposals](competitive-matches.md#vote-on-proposals). Players can
also queue the next map with `mymap` or pick it when a match ends, where the
server allows it; see [The map queue](server-setup.md#the-map-queue).

### Timeouts

**New in 0.15.0.** A team server can give each side timeouts. The operator
sets `si_timeouts` (how many each side gets, `0` for none) and
`si_timeoutLength` (seconds). Any player on a team then types `timeout` in
the console to pause the match for their side, and `timein` to end it early;
the whole server sees who called it and when play resumes, after a short
countdown. The host, a signed-in referee or the dedicated console can also
`pause` the match without spending a timeout, and `timein` or `unpause` ends
that pause. Duel servers keep their timeouts to managed matches for now.

While the match is paused:

- chat, the scoreboard, the in-match menu and looking around keep working;
- movement, attacks, damage and the rest of the game stay frozen;
- the match clock and the respawn, item, powerup and other game timers stop,
  and pick up where they left off;
- play resumes after a countdown.

## Match rules

These apply to casual games. A [managed match](competitive-matches.md) adds
its own rules on top.

### Warmup and starting

- With warmup on, `si_minPlayers` counts active humans and bots together, and
  at least one human must be playing.
- Only humans count towards the ready percentage (`si_warmupReadyPercentage`).
  Duel needs both seated players ready; waiting spectators do not count.
- Team games need players on both sides. With `si_teamForcePresent 1` both
  teams need `si_teamSizeMin` players; with `0`, one side must reach it. Red
  Rover handles its own round rosters.
- `si_useReady 0` starts as soon as there are enough players, and `si_warmup 0`
  skips warmup for practice. A `si_warmupReadyPercentage` of `0` also turns off
  the ready percentage outside Duel; Duel still needs both players ready when
  ready-up is on.

Mark yourself ready with `ready`, withdraw with `notready`, or switch between
the two with `readyup`.

### Ending a match

- A tie at the frag or capture limit goes to sudden death. A tie when the clock
  runs out gets overtime. If both teams pass the score limit, the leader wins.
- Losing all the opponents ends live play, even with no time limit. If the last
  human leaves, the match is aborted and any bots wait for players.
- Tied scores share a place: `10, 10, 9` is first, first, third.

What follows a match, from the same map again to a shuffled rotation, is the
server's choice; see
[Maps, rotation and the map queue](server-setup.md#maps-rotation-and-the-map-queue).

### Duel

Duel is winner-stays. Only the two seated players are ranked; the queue stays
in the spectator list. Both finalists stay on the final scoreboard until the
review ends, then the loser goes to the back of the queue and the next
challenger takes the seat. The queue keeps arrival order, and disconnecting
removes you from it.

### Round modes

- Clan Arena, Freeze Tag and Red Rover reset the map between rounds while the
  match carries on.
- **Freeze Tag:** frozen bodies stay even after heavy damage. Stand within
  `si_freezeThawRadius` of a frozen teammate, with a clear view of them, for
  `si_freezeThawTime` seconds to thaw them and score a point. Stepping away
  undoes some of the progress. `si_freezeThawThroughSurface 1` lets you thaw
  through walls.
- **Red Rover:** in a managed match, a defeated player moves to the other side
  for the rest of the round without counting as a team change. Roster seats
  and captain roles stay, and the next round puts everyone back on their home
  side. Team text and voice chat follow the side a player is on now.

### Objective modes

- **DeadZone:** control time counts only during live play. Dead or spectating
  players, and players without a required token, cannot hold or contest the
  zone. Any eligible opponent in the zone contests it, whoever entered first.
- **One Flag** modes can use the original CTF maps. On a map with both team
  bases but no neutral flag, openQ4 places the neutral flag at the spare pickup
  spot nearest to both bases. A neutral flag placed by the mapper, or a map's
  own One Flag layout, always comes first.

Markers on the HUD show where the flags and zones are and who holds them; see
[Multiplayer objective markers](gameplay-settings.md#multiplayer-objective-markers).

### Spectators, idle players and disconnects

**New in 0.15.0.** Three server policies, all off unless the operator turns
them on:

- `si_spectatorLock 1` keeps spectators out of a match while it is live; they
  join when it ends. `2` also keeps players from leaving to spectate. Someone
  who connects while the match is live is a spectator until it ends, and is
  told so.
- `g_inactivity <seconds>` warns a playing human who has sent no input for
  that long, then moves them to spectators (or drops them when the server has
  no spectators). Bots are never counted, and a pause does not count as idle
  time.
- `g_pauseOnDisconnect <seconds>` pauses a live team match when a playing human
  loses their connection, so they can reconnect or a replacement can join,
  instead of forfeiting an emptied side at once. Each side gets one such pause
  a match. The pause ends as soon as the side has its player back, or on its
  own when the time runs out, and the usual checks run then. A player the
  server removed itself (a kick, an idle drop) does not pause the match. While
  the pause lasts, that side may take a player in even with `si_spectatorLock`
  on.

### Your identity, and coming back after a dropped connection

> **After 0.15.0.** This describes the development build; 0.15.0 has none of it.

openQ4 lets each server recognise you when you come back, with no account and
nothing sent anywhere else. The first time a server asks, your game makes a
random key and keeps it in `identity.key` in your save folder. Each server sees
a different key derived from it, so two servers cannot tell that they have
seen the same player. Your identity is sixteen letters and digits, such as
`3f9a0c1d22e4b781`; `identity` shows yours on the server you are on.

- **A dropped connection costs you nothing.** If you leave a casual match and
  come back within five minutes (the server's `g_reconnectRestore`), you get
  your team and your score back. Tourney is the exception: its bracket keeps
  its own places.
- **No identity?** Set `ui_identity 0` and your game sends none. When the
  server keeps places, it tells you a six-letter return code instead; type
  `ghost <code>` after you reconnect.
- **Starting again.** `identityReset confirm` gives you a new identity on every
  server. Bans and rankings tied to the old one no longer apply to you, and
  no server recognises you any more. Deleting `identity.key` does the same.
- **Your match history.** When a match ends, your game adds a line to
  `profile/history.ndjson` in your save folder: when, where, the map and mode,
  your score, your place and the result. `history` lists your last ten
  matches. Nothing is uploaded.
- **Rankings.** A server can keep a local ranking table (see
  [Rankings](server-setup.md#identities-returning-players-and-rankings)).
  `rcon rankings`, where the server allows it, shows it.

Servers older than this build never ask, and your game sends nothing to them.

## Your multiplayer settings

**Multiplayer → Player Setup**, or the in-match menu's **Settings** tab, holds
the settings that are yours rather than the server's:

- **Player:** your name, clan tag, player model, rail colour and
  [weapon order](multiplayer-client-options.md#weapon-preference-order)
  (**New in 0.15.0**).
- **HUD:** hit sounds, crosshair names and ammunition, item timers, team
  locations, a clock, and your spectator speed and follow view (**Not in
  0.14.1**). See [Multiplayer client options](multiplayer-client-options.md).
- **Opponents** and **Teammates:** outlines, rim light, bright skins and
  forced models. See
  [Multiplayer player visibility](display-settings.md#multiplayer-player-visibility-and-item-styles).

Also worth knowing:

- [Damage numbers](gameplay-settings.md#damage-numbers-multiplayer) float the
  damage you deal above the player you hit.
- Your **Buy Menu**, **Chat**, **Quick Chat**, **Voice Chat** and **Grappling
  Hook** keys are under **Settings → Controls**; see
  [Input settings](input-settings.md#changing-key-bindings).
- On a slow or shared connection, lower `net_clientMaxRate` below your real
  download speed if matches stutter; see
  [Multiplayer networking](multiplayer-networking.md#connection).
- Experimental prediction settings change how other players and your own
  shots are drawn on a connection with delay; see
  [How other players and your shots are shown](multiplayer-networking.md#how-other-players-and-your-shots-are-shown).

A server can limit some of these, such as bright skins and item timers, for
everyone on it; see
[What a server may restrict](multiplayer-client-options.md#what-a-server-may-restrict).

## Watch and record matches

**Multiplayer → Demos** lists your recordings. A multi-view demo records the
whole match, so you can follow any player, fly freely, seek and jump to
markers. The server records them with `recordMVD`, and a managed match records
one by itself. See [Demos and multi-view demos](multiview-demos.md).

## Competitive matches

A server can run managed matches instead of casual ones: server-run ready
checks, captains and referees, tactical timeouts, best-of series with map
vetoes and a saved match report. Managed matches are still experimental and
not ready for tournaments. See [Competitive matches](competitive-matches.md).

## Troubleshooting

| Problem | What to try |
|---|---|
| A server says **Network protocol mismatch**, or one you expect is missing from the list | It runs a different openQ4 version, or retail Quake 4. Everyone needs the same openQ4 version; see [Before you play](#before-you-play). |
| A pure server reports missing packages, or the game says its data is out of sync | Your openQ4 version or game files differ from the server's. Install the same openQ4 package as the server, into a fresh folder. See [Pure multiplayer and local game modules](server-security.md#pure-multiplayer-and-local-game-modules). |
| A LAN server does not show up | The LAN scan looks only at ports `28004` to `28011`. Join a server on another port by address. |
| An IPv6 server never answers, or never appears in the list | Your network may block split packets. Join over IPv4; see [IPv6 and packet size](multiplayer-networking.md#ipv6-and-packet-size). |
| Friends cannot reach your server | Open the server's UDP port in your firewall and forward it in your router, then test from outside your network. See [Firewall and router](server-setup.md#firewall-and-router). |
| Matches stutter on a slow line | Lower `net_clientMaxRate` a little below your download speed before you connect. |
| Voice chat is silent | Turn on **Microphone access** on the in-match **Voice** tab, bind **Voice Chat**, and check your system's recording device. See [If it does not work](multiplayer-chat.md#if-it-does-not-work). |

To report a problem, include the log file; [Getting Started](getting-started.md#reporting-a-problem)
shows how to make one.

## All multiplayer guides

**Play**

- [Multiplayer networking](multiplayer-networking.md): the server browser,
  connecting by address over IPv4 or IPv6, prediction and lag compensation,
  and connection settings.
- [Multiplayer chat](multiplayer-chat.md): text chat, the quick-chat wheel and
  voice chat.
- [Multiplayer client options](multiplayer-client-options.md) (**Not in
  0.14.1**): chat tokens, hit sounds by damage, weapon order, item timers and
  team locations.
- [Buying in multiplayer](buying.md) (**New in 0.15.0**): the buy menu, prices
  and credits.
- [Classic multiplayer ruleset](classic-ruleset.md) (**New in 0.15.0**):
  Quake 2-style weapons, tiered armour, Threewave CTF scoring and the
  grappling hook.
- [Demos and multi-view demos](multiview-demos.md): watch, record and recover
  matches.

**Host**

- [Server setup](server-setup.md): hosting from the game, dedicated servers,
  ports, server settings, modifiers and presets, map rotation and bots.
- [Server and remote-console security](server-security.md): remote
  administration, the Admin page, pure servers and retail clients.

**Compete**

- [Competitive matches](competitive-matches.md) (experimental): Match Control,
  roles, series and map vetoes, match reports and running a managed server.
