# Voice generation breakdown (ElevenLabs)

Every voice line openQ4 needs that no installed recording can supply, for
single player (The Awakening, the stock campaign) and multiplayer, with the
voices, the consent questions, the cost and the work around it. Audited on 16
November 2026 from the source, the installed Awakening content (`q4xbase`), the
retail Quake 4 assets and the Steam paks. The machine-readable list is
[`voice-generation-lines.json`](voice-generation-lines.json), one entry per line
with its group, ID, voice, text, tier and notes.

## Summary

| Group | Clips | Characters | Tier | Voice |
| --- | --- | --- | --- | --- |
| Awakening m01 battle radio | 29 | 1,466 | required | 11 anonymous marines (stock or designed voices), Markswell (3) |
| Multiplayer announcer: missing events | 51 | 704 | required | the announcer |
| Multiplayer announcer: optional calls | 10 | 145 | optional | the announcer |
| Multiplayer announcer: mode and modifier names | 21 | 215 | optional | the announcer |
| Multiplayer announcer: end-game award names | 10 | 90 | optional | the announcer |
| Multiplayer quick chat | 16 | 156 | optional | a Marine and a Strogg player voice |
| Multiplayer announcer: the stock set re-voiced | 70 | 988 | only with a new announcer voice | the announcer |
| **Required** | **80** | **2,170** | | |
| **Everything** | **207** | **3,764** | | |

Not counted: the 100 m01 draft lines whose scenes the shipped maps do not have
(their scenes must exist first; see the
[register](awakening-missing-voice-lines.md#not-placed-no-scene-in-the-shipped-mission-100)),
and optional Arena Campaign introductions ("Introducing..." and 16 fighter
names).

The stock Quake 4 campaign has nothing to commission: every reachable retail
line that is missing is also missing its text, so retail is silent there too.

## Decisions before generating

1. **The announcer's voice.** Every new multiplayer line has to sound like the
   announcer it plays beside. The stock announcer is a real actor's
   performance, and ElevenLabs requires the voice owner's consent to clone an
   identifiable person (the cloning flow asks you to confirm it, and accounts
   that clone without it are banned). Three ways forward:
   - **Consent.** Clone the stock announcer with the permission of the actor
     and the rights holder. Then only the 51 required lines (plus any optional
     ones) are needed. Unlikely to be quick.
   - **A new openQ4 announcer (recommended).** Design an original voice with
     ElevenLabs Voice Design and record the whole set with it: the 51 new
     lines and the 70 stock lines, so a match never mixes two announcers.
     Ship it as a selectable announcer, keeping the stock one available.
     That adds the 70 "re-voiced" clips and a small selection mechanism.
   - **New lines in a different voice.** Cheapest, and every round mode would
     change voice mid-match. Not recommended.
2. **Markswell's third radio line.** `vo_4_2_11` and `vo_4_2_13` have recorded
   takes of him with the same or nearly the same words. "Breach those bunkers,
   Sergeant." (`vo_4_2_12`) has none: voicing it in his voice is cloning a real
   actor, with the same consent rule. Without consent: use his two takes and
   leave `vo_4_2_12` out (or give it to an anonymous radio voice).
3. **The anonymous marines** have no established voice. Each of the eleven
   speakers gets one voice for all of its lines, from the voice library or
   Voice Design: no consent question. The radio sound is added afterwards in
   openQ4's own processing, to match the battle's recorded radio.
4. **Listen before commissioning.** 922 recordings in the expansion's content
   are reached by nothing, about 48 minutes. Among them,
   `sound/vo_english/m04_stranarus_trench/` holds 68 files numbered like the m01
   draft. Their lengths fit some of the draft's lines (`4_5_2` to `4_5_5`, the
   Tiger squad ambush and "What the hell is that? / Oh shit!") and not others,
   so they are a different revision. A local transcription pass (Whisper)
   would settle which draft lines already exist and recover the words of the
   orphan takes. It needs a download (the Python package and a model), so it
   waits for a go-ahead.

## Fixes that need no generation

These came out of the same audit. Each is a recording the content already has.

| Gap | Lines | Finding | Fix |
| --- | --- | --- | --- |
| Cryofac: Darnell's radio intro (m08) | 2 | `nava_herightsrg.wav` (1.1 s) and `darn_dntwrryct.wav` (1.75 s) are recorded; only their declarations and captions are missing | Play them by path from the repairs table; write the captions by ear |
| m01 "+use" replies | 23 | The script sets talk keys to other keys' names (`"lipsync_darn_theywait"`) instead of sound names (`vo_darn_theywait`), so every reply plays the 186 ms missing-sample tone | Resolve the extra level in the repairs, for the m01 maps |
| m01 Darnell and Tak trench lines | 2 | Recorded under misspelt file names (`dar_tre_reloadig.wav`, `tak_tre_needcover.wav`); nothing calls them | None needed; only if a scene is found for them |
| m02 later "+use" replies | 14 | All recorded (33.5 s); keyed only in unused definitions | A placement question, like the Cryofac replies |
| m07 race radio | 45 | Recorded under `m08_race/` with caption text; no sound declarations | A placement question: the race's chatter script is switched off |
| Multiplayer team tie | — | A tied team match tells the Strogg "You win" (`MultiplayerGame.cpp:20382` compares with `>`) | A code fix, and the "Match drawn" line |
| Multiplayer spectators at match and round end | — | Judged by a stale team: may hear a win or loss line | A code fix; the caster lines above |
| Voice-command chat | — | `clientVoiceChat` looks for `snd_voc_*` keys no content defines, so it always prints "Voice command not found" | The quick-chat set above is the natural content for it |

## Output and integration

- **Format.** Awakening voice is 44.1 kHz, 16-bit mono WAV
  (`sound/vo_english/<mission>/`). The multiplayer announcer is 22.05 kHz mono
  Ogg Vorbis (`sound/vo_english/mp/`). Match loudness to the clips each line
  plays beside.
- **Where it ships.** Generated audio is original openQ4 content, so it can go
  in a pack under `content/baseoq4/` (or a separate voice pack if it should be
  optional). Nothing derived from the retail recordings goes into Git.
- **Declarations.** One sound declaration per line under the ID above, and a
  lip-sync declaration carrying the caption, so captions and subtitles work.
  The m01 radio placement picks its lines up with no further code.
- **Multiplayer code.** Each new announcer line needs a new `announcerSound_t`
  value before `AS_NUM_SOUNDS` and a matching `announcerSoundDefs[]` entry
  (index-parallel, no compile-time check). The aliases at
  `MultiplayerGame.h:227-235` then point at the new values. The values must
  stay under 256 (`ReceiveAnnouncer` reads a byte); there are 57 today.
- **Localization.** The game's text is localized; its voice is English only.
  ElevenLabs can render the same lines in other languages later.
- **Pipeline.** A script reads `voice-generation-lines.json`, calls the
  ElevenLabs API with each line's voice, writes the takes to `.tmp/`, and,
  once a take is chosen, converts and writes the declarations. The API key is
  read from `ELEVENLABS_API_KEY`, set by you.

## Cost

ElevenLabs bills credits per character: about one per character on its main
models, half on the faster ones. Plans, from third-party summaries of the
pricing page (check the page itself before buying):

| Plan | Price | Credits a month | Notes |
| --- | --- | --- | --- |
| Free | $0 | 10,000 | No commercial rights; attribution required |
| Starter | about $6 | 30,000 | Commercial rights; instant voice cloning |
| Creator | about $22 | 121,000 | Professional voice cloning; higher quality output |

The required lines are about 2,200 characters and everything is about 3,800.
At around five takes a line, that is roughly 11,000 and 19,000 credits, before
Voice Design previews, which draw on the same credits. **Starter covers the
required lines and probably everything in one month; Creator leaves room for
retakes and voice design.** Either can be cancelled after the month.

## The lines

#### Awakening m01 battle radio (29 clips, 1466 characters)

| ID | Voice | Text | Notes |
| --- | --- | --- | --- |
| `vo_4_2_2_3` | marine9 | Objective is lost, Command, please advise! |  |
| `vo_4_2_2_4` | marine9 | Oh fuck!  They're already here! |  |
| `vo_4_2_2_5` | marine8 | I need an evac now.  Now, dammit, now! |  |
| `vo_4_2_2_6` | marine8 | We've lost contact with Raptor and Venom squads. |  |
| `vo_4_2_2_1` | marine10 | They're all over us!  Oh god - (Static) |  |
| `vo_4_2_2_2` | marine10 | It's no good, Command.  We're gonna have to take the alternate route. |  |
| `vo_4_1_10` | marine7 | We're taking heavy fire, we've lost two port thrusters, and. |  |
| `vo_4_2_2_13` | marine4 | This isn't an assault, Command.  It's a fucking massacre. |  |
| `vo_4_2_2_12` | marine5 | They won't stop coming!  We've killed hundreds, they keep coming! |  |
| `vo_4_5_1` | marine3 | Tiger squad! |  |
| `vo_4_5_2` | marine9 | They were waiting for us! | m04_stranarus_trench/4_5_2.wav may already be this line: listen first |
| `vo_4_5_3` | marine4 | Tiger squad, fall back.  Fall back! | m04_stranarus_trench/4_5_3.wav may already be this line: listen first |
| `vo_4_2_2_7` | marine7 | Raptor squad is down.  They ambushed us.  I'm the only one left, the entire squad was wiped out. |  |
| `vo_4_2_2_8` | marine7 | Command, where is our fucking support? |  |
| `vo_4_2_11` | Markswell | Get moving, Marine. | recorded take vo_marks_getmov has the same words |
| `vo_4_2_2_11` | marine5 | Watch the turrets!  Watch the-- |  |
| `vo_4_2_12` | Markswell | Breach those bunkers, Sergeant. | no recorded take: needs his voice |
| `vo_4_2_2_14` | marine4 | Bunker eight-nine-bravo is clear! |  |
| `vo_4_2_13` | Markswell | You have your orders, Sergeant.  Find a way behind those turrets. | recorded take vo_marks_youhaveords is close |
| `vo_4_5_4` | marine12 | What the hell is that? | m04_stranarus_trench/4_5_4.wav may already be this line: listen first |
| `vo_4_5_5` | marine12 | Oh shit! | m04_stranarus_trench/4_5_5.wav may already be this line: listen first |
| `vo_4_2_2_9` | marine6 | Eastern approach is stalled.  They've got us pinned in.  We're not gonna last long. |  |
| `vo_4_2_2_15` | marine3 | We've breached the Northern wall.  We're 'go' for infiltration. |  |
| `vo_4_2_2_16` | marine3 | Mongoose squad is in position, awaiting further orders, Command. |  |
| `vo_4_2_2_17` | marine2 | (Cough) Command (Cough) Sector two-four-nine is lost. |  |
| `vo_4_2_2_18` | marine2 | It's a clusterfuck, Command, two squads killed right in front of me.  We need an updated sitrep.  Now! |  |
| `vo_4_2_2_10` | marine6 | Objective secure. |  |
| `vo_4_2_2_19` | marine1 | They're all dead!  All of'em.  I'm not letting'em take me, Command.  You hear me?  They won't take me! (Gunshot) |  |
| `vo_4_2_2_20` | marine1 | Oh shit!  They're taking us apart.  Twelfth mobile division was just fucking incinerated. You copy, Command? |  |

#### Multiplayer announcer: missing events (51 clips, 704 characters)

| ID | Voice | Text | Notes |
| --- | --- | --- | --- |
| `round_1..round_20` | announcer | Round one ... Round twenty | 20 clips; Clan Arena/Freeze Tag first-to-8 can reach round 15, si_roundLimit allows 99 |
| `round_final` | announcer | Final round | fallback past round twenty, and the deciding round |
| `match_point` | announcer | Match point |  |
| `round_won` | announcer | Round won | replaces the 'You score' alias (RoundGameState.cpp:927) |
| `round_lost` | announcer | Round lost | replaces the 'Enemy scores' alias (:929) |
| `round_draw` | announcer | Round draw | replaces the 'Teams are tied' alias (:925) |
| `round_marines_win` | announcer | Marines win the round | spectators and casters |
| `round_strogg_win` | announcer | Strogg win the round | spectators and casters |
| `last_standing` | announcer | Last one standing | replaces the 'One frag left' alias (RoundGameState.cpp:784, RoundModes.cpp:817) |
| `one_enemy_left` | announcer | One enemy remaining | the other side's view of the same event |
| `overtime` | announcer | Overtime! | timed overtime is text only today (GameState.cpp:121) |
| `match_marines_win` | announcer | Marines win the match | managed match end is silent for teams (MultiplayerGame.cpp:11656) |
| `match_strogg_win` | announcer | Strogg win the match |  |
| `match_draw` | announcer | Match drawn | a tie plays 'You win' to Strogg today (MultiplayerGame.cpp:20382, a bug) |
| `match_aborted` | announcer | Match aborted |  |
| `forfeit` | announcer | Forfeit | text only today |
| `mercy_rule` | announcer | Mercy rule | text only today |
| `timeout` | announcer | Timeout | MP-12 timeouts are text only (MultiplayerGame.cpp:23882) |
| `match_paused` | announcer | Match paused |  |
| `match_resumed` | announcer | Match resumed | 'Time in' (MultiplayerGame.cpp:23897); the resume countdown reuses three/two/one |
| `player_disconnected` | announcer | Player disconnected | disconnect pause (MultiplayerGame.cpp:23886) |
| `flag_marines_have` | announcer | Marines have the flag | CTF caster set: spectators hear nothing today (GameState.cpp:1557-1640) |
| `flag_strogg_have` | announcer | Strogg have the flag | CTF caster set |
| `flag_marines_dropped` | announcer | Marine flag dropped | CTF caster set |
| `flag_strogg_dropped` | announcer | Strogg flag dropped | CTF caster set |
| `flag_marines_returned` | announcer | Marine flag returned | CTF caster set |
| `flag_strogg_returned` | announcer | Strogg flag returned | CTF caster set |
| `marines_score` | announcer | Marines score | CTF caster set |
| `strogg_score` | announcer | Strogg score | CTF caster set |
| `armor_regeneration` | announcer | Armor regeneration | Awakening armour regen powerup plays 'Regeneration' (Player.cpp:6247) |
| `quad_loose` | announcer | Quad Damage is loose | Quad Hog modifier, text only (MultiplayerGame.cpp:27903) |
| `quad_hog` | announcer | You are the Quad Hog | Quad Hog modifier (MultiplayerGame.cpp:27856) |

#### Multiplayer announcer: optional calls (10 clips, 145 characters)

| ID | Voice | Text | Notes |
| --- | --- | --- | --- |
| `thirty_seconds` | announcer | Thirty seconds | clock and round timer |
| `ten_seconds` | announcer | Ten seconds |  |
| `overtime_2` | announcer | Second overtime | periods are unlimited (MultiplayerGame.cpp:7335) |
| `overtime_3` | announcer | Third overtime |  |
| `one_capture_left` | announcer | One capture remaining | CTF limit warning |
| `vote_next_map` | announcer | Vote for the next map | MP-13 end-of-match map pick is silent |
| `you_are_frozen` | announcer | You've been frozen | Freeze Tag, text only (RoundModes.cpp:286) |
| `thawed` | announcer | Thawed | Freeze Tag |
| `teammate_frozen` | announcer | Teammate frozen | Freeze Tag |
| `adrenaline` | announcer | Adrenaline | silent pickup (Player.cpp:6234) |

#### Multiplayer announcer: mode and modifier names (21 clips, 215 characters)

| ID | Voice | Text | Notes |
| --- | --- | --- | --- |
| `name_deathmatch` | announcer | Deathmatch | no code announces a mode today; needs a match-start intro |
| `name_team_deathmatch` | announcer | Team Deathmatch | no code announces a mode today; needs a match-start intro |
| `name_tourney` | announcer | Tourney | no code announces a mode today; needs a match-start intro |
| `name_duel` | announcer | Duel | no code announces a mode today; needs a match-start intro |
| `name_capture_the_flag` | announcer | Capture the Flag | no code announces a mode today; needs a match-start intro |
| `name_one_flag_ctf` | announcer | One Flag CTF | no code announces a mode today; needs a match-start intro |
| `name_arena_ctf` | announcer | Arena CTF | no code announces a mode today; needs a match-start intro |
| `name_deadzone` | announcer | DeadZone | no code announces a mode today; needs a match-start intro |
| `name_clan_arena` | announcer | Clan Arena | no code announces a mode today; needs a match-start intro |
| `name_freeze_tag` | announcer | Freeze Tag | no code announces a mode today; needs a match-start intro |
| `name_red_rover` | announcer | Red Rover | no code announces a mode today; needs a match-start intro |
| `name_instagib` | announcer | Instagib | no code announces a mode today; needs a match-start intro |
| `name_quad_hog` | announcer | Quad Hog | no code announces a mode today; needs a match-start intro |
| `name_vampiric_damage` | announcer | Vampiric Damage | no code announces a mode today; needs a match-start intro |
| `name_weapons_frenzy` | announcer | Weapons Frenzy | no code announces a mode today; needs a match-start intro |
| `name_rocket_arena` | announcer | Rocket Arena | no code announces a mode today; needs a match-start intro |
| `name_rail_arena` | announcer | Rail Arena | no code announces a mode today; needs a match-start intro |
| `name_nadefest` | announcer | NadeFest | no code announces a mode today; needs a match-start intro |
| `name_classic` | announcer | Classic | no code announces a mode today; needs a match-start intro |
| `name_instagib_ctf` | announcer | Instagib CTF | no code announces a mode today; needs a match-start intro |
| `name_classic_ctf` | announcer | Classic CTF | no code announces a mode today; needs a match-start intro |

#### Multiplayer announcer: end-game award names (10 clips, 90 characters)

| ID | Voice | Text | Notes |
| --- | --- | --- | --- |
| `award_lemming` | announcer | Lemming | text only in retail too; for an intermission awards reel (MP-12/MP-17) |
| `award_rail_master` | announcer | Rail Master | text only in retail too; for an intermission awards reel (MP-12/MP-17) |
| `award_rocket_sauce` | announcer | Rocket Sauce | text only in retail too; for an intermission awards reel (MP-12/MP-17) |
| `award_brawler` | announcer | Brawler | text only in retail too; for an intermission awards reel (MP-12/MP-17) |
| `award_sniper` | announcer | Sniper | text only in retail too; for an intermission awards reel (MP-12/MP-17) |
| `award_critical_failure` | announcer | Critical Failure | text only in retail too; for an intermission awards reel (MP-12/MP-17) |
| `award_team_player` | announcer | Team Player | text only in retail too; for an intermission awards reel (MP-12/MP-17) |
| `award_accuracy` | announcer | Accuracy | text only in retail too; for an intermission awards reel (MP-12/MP-17) |
| `award_frags` | announcer | Frags | text only in retail too; for an intermission awards reel (MP-12/MP-17) |
| `award_perfect` | announcer | Perfect | text only in retail too; for an intermission awards reel (MP-12/MP-17) |

#### Multiplayer quick chat (16 clips, 156 characters)

| ID | Voice | Text | Notes |
| --- | --- | --- | --- |
| `chat_marine_attack` | Marine player | Attack! | the quick-chat wheel is text only (QuickChat.inl:140-152); clientVoiceChat has no snd_voc_* lines |
| `chat_marine_defend` | Marine player | Defend! | the quick-chat wheel is text only (QuickChat.inl:140-152); clientVoiceChat has no snd_voc_* lines |
| `chat_marine_need_backup` | Marine player | Need backup! | the quick-chat wheel is text only (QuickChat.inl:140-152); clientVoiceChat has no snd_voc_* lines |
| `chat_marine_enemy_incoming` | Marine player | Enemy incoming! | the quick-chat wheel is text only (QuickChat.inl:140-152); clientVoiceChat has no snd_voc_* lines |
| `chat_marine_on_my_way` | Marine player | On my way. | the quick-chat wheel is text only (QuickChat.inl:140-152); clientVoiceChat has no snd_voc_* lines |
| `chat_marine_thanks` | Marine player | Thanks! | the quick-chat wheel is text only (QuickChat.inl:140-152); clientVoiceChat has no snd_voc_* lines |
| `chat_marine_nice_shot` | Marine player | Nice shot! | the quick-chat wheel is text only (QuickChat.inl:140-152); clientVoiceChat has no snd_voc_* lines |
| `chat_marine_good_game` | Marine player | Good game. | the quick-chat wheel is text only (QuickChat.inl:140-152); clientVoiceChat has no snd_voc_* lines |
| `chat_strogg_attack` | Strogg player | Attack! | the quick-chat wheel is text only (QuickChat.inl:140-152); clientVoiceChat has no snd_voc_* lines |
| `chat_strogg_defend` | Strogg player | Defend! | the quick-chat wheel is text only (QuickChat.inl:140-152); clientVoiceChat has no snd_voc_* lines |
| `chat_strogg_need_backup` | Strogg player | Need backup! | the quick-chat wheel is text only (QuickChat.inl:140-152); clientVoiceChat has no snd_voc_* lines |
| `chat_strogg_enemy_incoming` | Strogg player | Enemy incoming! | the quick-chat wheel is text only (QuickChat.inl:140-152); clientVoiceChat has no snd_voc_* lines |
| `chat_strogg_on_my_way` | Strogg player | On my way. | the quick-chat wheel is text only (QuickChat.inl:140-152); clientVoiceChat has no snd_voc_* lines |
| `chat_strogg_thanks` | Strogg player | Thanks! | the quick-chat wheel is text only (QuickChat.inl:140-152); clientVoiceChat has no snd_voc_* lines |
| `chat_strogg_nice_shot` | Strogg player | Nice shot! | the quick-chat wheel is text only (QuickChat.inl:140-152); clientVoiceChat has no snd_voc_* lines |
| `chat_strogg_good_game` | Strogg player | Good game. | the quick-chat wheel is text only (QuickChat.inl:140-152); clientVoiceChat has no snd_voc_* lines |

#### Multiplayer announcer: stock set re-voiced (only for a new announcer voice) (70 clips, 988 characters)

| ID | Voice | Text | Notes |
| --- | --- | --- | --- |
| `stock_three` | announcer | Three | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_two` | announcer | Two | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_one` | announcer | One | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_you_win` | announcer | You win | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_you_lose` | announcer | You lose | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_fight` | announcer | Fight! | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_sudden_death` | announcer | Sudden death | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_vote_failed` | announcer | Vote failed | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_vote_now` | announcer | Vote now | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_vote_passed` | announcer | Vote passed | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_one_frag` | announcer | One frag left | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_two_frags` | announcer | Two frags left | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_three_frags` | announcer | Three frags left | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_one_minute` | announcer | One minute remaining | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_five_minute` | announcer | Five minutes remaining | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_prepare_to_fight` | announcer | Prepare to fight | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_quad_damage` | announcer | Quad Damage | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_regeneration` | announcer | Regeneration | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_haste` | announcer | Haste | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_invisibility` | announcer | Invisibility | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_tied_lead` | announcer | You are tied for the lead | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_taken_lead` | announcer | You have taken the lead | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_lost_lead` | announcer | You have lost the lead | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_enemy_score` | announcer | The enemy scores | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_you_score` | announcer | You score | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_teams_tied` | announcer | Teams are tied | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_strogg_lead` | announcer | Strogg lead | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_marines_lead` | announcer | Marines lead | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_join_marine` | announcer | You are on the Marines | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_join_strogg` | announcer | You are on the Strogg | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_ctf_you_have_flag` | announcer | You have the flag | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_ctf_team_has_flag` | announcer | Your team has the flag | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_ctf_enemy_has_flag` | announcer | The enemy has your flag | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_ctf_team_drops` | announcer | Your team dropped the flag | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_ctf_enemy_drops` | announcer | The enemy dropped your flag | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_ctf_flag_returned` | announcer | Your flag was returned | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_ctf_enemy_returns` | announcer | The enemy flag was returned | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_award_capture` | announcer | Capture | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_award_humiliation` | announcer | Humiliation | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_award_impressive` | announcer | Impressive | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_award_excellent` | announcer | Excellent | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_award_assist` | announcer | Assist | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_award_defense` | announcer | Defense | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_award_combo_kill` | announcer | Combo kill | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_award_rampage` | announcer | Rampage | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_award_holy_shit` | announcer | Holy shit! | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_tourney_advance` | announcer | You advance | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_tourney_start` | announcer | The tournament begins | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_tourney_done` | announcer | The tournament is over | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_tourney_eliminated` | announcer | You have been eliminated | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_tourney_won` | announcer | You won the tournament | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_tourney_prelims` | announcer | Preliminaries | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_tourney_quarter_finals` | announcer | Quarter finals | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_tourney_semi_finals` | announcer | Semi finals | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_tourney_final_match` | announcer | Final match | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_tourney_waiting` | announcer | Waiting for an arena | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_team_ammo_regen` | announcer | Team ammo regeneration | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_team_doubler` | announcer | Team doubler | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_guard` | announcer | Guard | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_scout` | announcer | Scout | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_doubler` | announcer | Doubler | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_ammo_regen` | announcer | Ammo regeneration | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_tourney_arena_1` | announcer | Join arena one | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_tourney_arena_2` | announcer | Join arena two | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_tourney_arena_3` | announcer | Join arena three | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_tourney_arena_4` | announcer | Join arena four | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_tourney_arena_5` | announcer | Join arena five | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_tourney_arena_6` | announcer | Join arena six | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_tourney_arena_7` | announcer | Join arena seven | exact retail words unconfirmed: no transcripts ship; listen before recording |
| `stock_tourney_arena_8` | announcer | Join arena eight | exact retail words unconfirmed: no transcripts ship; listen before recording |
