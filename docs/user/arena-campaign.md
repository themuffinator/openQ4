# Arena Campaign

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

> **Experimental.** The Arena Campaign and multiplayer are still under active development. Expect rough edges, bugs and behaviour that changes between versions.

The Arena Campaign is a single-player ladder of bot matches on Quake 4's stock multiplayer maps, against openQ4's character bots. Open **Single Player → Arena** to play it; **Single Player → Campaign** holds the story campaigns (see [Campaigns](campaigns.md)).

You need no internet connection, public server, community map pack or extra game content. Each match runs as a private multiplayer game on your own computer, so the bots play by the same scoring, team and match rules as human players.

## How the ladder works

The circuit has five tiers of four matches each, twenty matches in total:

1. The first three matches in a tier are available together.
2. Winning all three unlocks that tier's boss match.
3. Winning the boss match unlocks the next tier.
4. A match you lose stays available for another attempt.
5. A draw does not award a win or unlock progress; replay it to settle the match.
6. Defeating the Makron in the final boss duel completes the campaign.

You can replay any completed match without losing progress. After a result, **Retry** reruns a defeat, **Replay** repeats a victory or draw, and **Next** moves to the next match you still have to win: the rest of the tier first, then the tiers after it.

The browser shows your route to the next unlock: a progress bar, completion marks, the boss match's highlight and a tier briefing. A tier with some qualifiers won reads **IN PROGRESS**. A locked boss stays hidden, in the match list and the tier briefing alike, until you win all three qualifiers.

Arena screens use the standard menu confirm and back controls on keyboard and controller. The recommended action receives focus when a screen opens: the next match after a victory, retry after a defeat, replay after a draw, and cancel in the reset prompt. **Escape** or the controller back action dismisses a result, cancels a reset, or moves back one menu level.

## Difficulty

Bot skill rises with each tier, from 1 in Boot Camp to 5 in the Final Gauntlet. Individual opponents may be one step easier or harder than their tier, which gives each roster some personality.

The **−** and **+** controls on the **DIFFICULTY** row shift the whole campaign while keeping that spread, through **TRAINING**, **ROOKIE**, **VETERAN** (the default), **NIGHTMARE** and **IMPOSSIBLE**. Bot skill never leaves the normal 1 to 5 range, so the lower settings make no further difference in Boot Camp and the higher ones none in the Final Gauntlet. The match panel shows the resulting bot skill before you start.

## Best results

Every match keeps its **best result**, shown across the foot of the match's picture in the browser: the outcome, the final score and the difficulty, for example **VICTORY 10 - 3 VETERAN**. A match you have not yet played to a result reads **No result yet**.

A victory beats a draw, and a draw beats a defeat. Between two results with the same outcome, the wider winning (or narrower losing) margin is better, then the harder difficulty. When a replay beats the record, the match report marks the score **NEW BEST**.

Only a match played out counts: one you abandon, or whose opponents leave, leaves the record unchanged. Matches won before openQ4 kept best results show **VICTORY** until you replay them. Best results are saved with your campaign progress and cleared by **Reset Progress**.

## Match presentation

An Arena match has its own ceremony from start to finish, in place of the ordinary multiplayer lobby and scoreboard:

1. **Challenge launch.** Confirming a match expands its challenge card over the map and fades into the arena. Input is held for a moment so one press cannot trigger two actions.
2. **Introductions.** Each fighter is introduced in turn, named over a slow, face-on shot of them. In team matches your teammates come first and the opposition last, each card naming the side ("TEAMMATE 1 OF 2", "OPPONENT 2 OF 3"). Nobody can move or fire during the introductions.
3. **Countdown.** A four-second cinematic countdown follows automatically, with no Ready button or warmup vote. The camera sweeps in towards you over the tier, the match's place in it, its name and its game type.
4. **Spawn-in.** The camera circles your character where it spawned, swings in behind your head and settles into your first-person view. The announcer counts "three, two, one" and **Fight** sounds as control returns. (Clan Arena and Red Rover count down their first round themselves.)
5. **The match.** Normal play, with the rules listed on the match panel.
6. **Final tableau.** When the match ends, the whole arena freezes: fighters, projectiles, moving parts and effects. The camera moves to the winner, and you can look around the frozen winner freely while they are announced, whether or not you survived the final frag. A draw is announced as a tie, with no winner.
7. **Scoreboard.** The full scoreboard takes the screen, headed by the match.
8. **Match stats.** The scoreboard gives way to detailed end-of-match statistics.
9. **Match report.** You return to the Arena browser, where the result appears in stages: the outcome, final score, awards earned, unlocks and the recommended next step.
10. **Campaign update.** Leaving a newly won report shows the campaign board record the match, fill the progress bar and highlight any newly opened boss match or tier. Control returns once the update has finished, with the next useful match selected.

**Escape** skips straight to the match report from the tableau, the scoreboard or the stats; so does **fire**, once each stage has been on screen for a moment. Fire also skips the rest of the introductions. The crosshair and HUD stay hidden during every cinematic shot.

The cinematic frame around the introductions and the final tableau fits your screen. Its bars always reach every screen edge, thickening on narrower or taller screens so the picture stays close to 16:9, and they never cover the match clock or the rest of the interface. The frame lifts
as control returns.

The result is decided the moment the match ends. Skipping the ceremony, leaving or even quitting the game during it still records that result.

These ceremonies belong to the Arena Campaign only. Private matches started from the multiplayer menus keep their normal ready-up and scoreboard.

## Pausing and leaving a match

An Arena match pauses like a story level. Press **Escape** (or the controller's menu button) during a match for the **MATCH PAUSED** menu, which shows the match, its tier, game type and map, the paused clock and the score:

- **Resume** returns to the match. Escape or the back button does the same.
- **Restart match** asks first, then restarts the same match on the spot: everyone respawns, the score and clock go back to zero and the introductions play again. Nothing is recorded. If an opponent has left, the map reloads instead so the full line-up returns.
- **Settings** opens the normal settings screens. The match stays paused, and backing out returns you to the pause menu.
- **Leave match** asks first, then returns you to the Arena browser with a **Match abandoned** report that offers **Retry**. Nothing is recorded and your progress is unchanged.

While the pause menu or settings are open, the bots, the match clock, projectiles, respawns and round timers all stop. Chat does not pause the match, because the bots answer it.

A match has to be played out to count. Leaving before the end any other way, for example with the `disconnect` console command, also gives a **Match abandoned** report. A match whose opponents all leave (for example when its bots are removed from the console) ends as **Match aborted**, with nothing recorded and **Retry** offered.

## Match rules

Arena matches have no spectator mode and no team picker. The campaign sets the roster, the sides and the number of players, so the join, spectate and team controls are refused for the whole match, and no game rule can bench you: a campaign Duel is a one-on-one deathmatch rather than the multiplayer tournament mode, and a tie never sends the trailing fighter to spectate during sudden death. In Clan Arena you still sit out the rest of a round after being eliminated, which is that mode's own rule, and return for the next round.

Any end-of-match awards you earn are listed on the match report beside the score and any unlock.

## Resetting progress

**Reset Progress** becomes available once you have played at least one match to a result. It clears every Arena win, best result and tier unlock, and asks for confirmation first. It does not touch your story-campaign saves, multiplayer settings or achievements.

## The circuit

| Tier | Match | Map | Game type | Featured roster |
| --- | --- | --- | --- | --- |
| Boot Camp | First Blood | The Fragging Yard (`mp/q4dm1`) | Duel | Bagby |
| Boot Camp | Crossfire | Sandstorm (`mp/q4dm2`) | Deathmatch | Anderson, Bagby, Tetzlaff |
| Boot Camp | Fireteam | The Lost Fleet (`mp/q4dm3`) | Team Deathmatch | Anderson, Bagby, Tetzlaff, Sorg, Strauss |
| Boot Camp | Transfer Test | Bloodwork (`mp/q4dm4`) | Duel | Sorg |
| Ironworks | Thorns | The Rose (`mp/q4dm5`) | Deathmatch | Sorg, Strauss, Hollenbeck |
| Ironworks | Shifting Lines | No Doctors (`mp/q4dm6`) | Red Rover | Sorg, Strauss, Hollenbeck, Marsh, Anderson |
| Ironworks | Last Squad Standing | Over The Edge (`mp/q4dm7`) | Clan Arena | Sorg, Strauss, Hollenbeck, Marsh, Tetzlaff |
| Ironworks | The Marshal | Railed (`mp/q4tourney1`) | Duel | Marsh |
| Blood Circuit | Longest Odds | The Longest Day (`mp/q4dm8`) | Deathmatch | Bidwell, Cortez, Morris, Rhodes |
| Blood Circuit | Campground Clash | Campgrounds Redux (`mp/q4dm9`) | Team Deathmatch | Bidwell, Cortez, Morris, Rhodes, Voss |
| Blood Circuit | Skeleton Crew | Skeleton Crew (`mp/q4dm11`) | Clan Arena | Bidwell, Cortez, Morris, Rhodes, Voss |
| Blood Circuit | Officer's Challenge | The Fragging Yard 1v1 (`mp/q4dm11v1`) | Duel | Voss |
| Strogg Trials | Central Pressure | Central Industrial (`mp/q4xdm10`) | Deathmatch | Gunner, Rhodes, Cortez, Voss |
| Strogg Trials | Red Shift | Stroyent Red (`mp/q4xdm13`) | Red Rover | Gunner, Rhodes, Cortez, Voss, Kane |
| Strogg Trials | Warforged | Warforged (`mp/q4xdm11`) | Team Deathmatch | Gunner, Kane, Sledge, Voss, Rhodes |
| Strogg Trials | Sledgehammer | Verticon (`mp/q4xtourney2`) | Duel | Sledge |
| Final Gauntlet | Last Alliance | Retrophobopolis (`mp/q4xdm14`) | Team Deathmatch | Kane, Sledge, Gunner, Morris, Voss |
| Final Gauntlet | Firewall | Firewall (`mp/q4xdm15`) | Clan Arena | Kane, Sledge, Gunner, Morris, Voss |
| Final Gauntlet | Champion's Circle | Skeleton Crew (`mp/q4dm11`) | Deathmatch | Kane, Sledge, Gunner, Morris, Voss |
| Final Gauntlet | The Makron | Stroggenomenon (`mp/q4xtourney1`) | Duel | Makron |

The team modes put five bots and you in the match, for balanced three-against-three sides. Red Rover moves players between teams as frags are scored. Clan Arena gives each player one life per round, so positioning and survival matter as much as aim.

## Maps and troubleshooting

Every campaign map comes from the official Quake 4 files openQ4 already needs. The optional `q4cmp` community map pack is deliberately not part of the ladder. The campaign also leaves out CTF, DeadZone and other objective modes: the bots fight, find their way around, collect items and follow the match rules, but do not yet pursue flags or map objectives.

Bots work out how to move around a map when it loads, so the first bot match on a map can take a brief moment to prepare. There is no separate bot file to download or install.

- **Missing Required Map.** Check your Quake 4 installation and openQ4's game-file locations using [Getting started](getting-started.md). Do not install loose replacement maps over the campaign maps.
- **Match Aborted.** If a match cannot start or its result cannot be recorded, the browser shows a **Match Aborted** report naming the match and the reason. The usual cause is another program, often a second copy of openQ4, already using the network port the match needs.
