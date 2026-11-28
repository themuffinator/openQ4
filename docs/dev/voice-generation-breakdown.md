# Voice generation breakdown (ElevenLabs)

Every voice line openQ4 needs that no installed recording can supply, for
single player (The Awakening, the stock campaign) and multiplayer, with the
voices, the consent questions, the cost and the work around it. Audited on 16
November 2026 from the source, the installed Awakening content (`q4xbase`), the
retail Quake 4 assets and the Steam paks. The machine-readable list is
[`voice-generation-lines.json`](voice-generation-lines.json), one entry per line
with its group, ID, voice, text, tier and notes. Schema 2 adds `voices`, the
profiles `tools/assets/elevenlabs_voice.py` designs and speaks with, and
optional per-line fields (`caption`, `preset`, `hive`, `reference_seconds`,
`original`, `target`, and `kind`/`duration_seconds`/`loop` for sound
effects).

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
| Awakening Valkaryne re-voiced | 37 (+6 sound effects) | 723 | required (2 optional), decided 28 November | the Valkaryne (designed, machine) |
| **Required** | **115** | **2,862** | | |
| **Everything** | **244 (+6 sound effects)** | **4,487** | | |

Not counted: the 100 m01 draft lines whose scenes the shipped maps do not have
(their scenes must exist first; see the
[register](awakening-missing-voice-lines.md#not-placed-no-scene-in-the-shipped-mission-100)),
and optional Arena Campaign introductions ("Introducing..." and 16 fighter
names).

The stock Quake 4 campaign has nothing to commission: every reachable retail
line that is missing is also missing its text, so retail is silent there too.

## The Valkaryne re-voiced

**Decision (the user, 28 November 2026).** The Valkaryne gets a new voice
for every line she has. She is visibly mechanical and soulless, so the voice is
heavily synthesised and menacing, and must not seem human. The alpha's takes are a
natural alto with a light metallic comb on the long ones. Next to a machine voice
they would sound like another character, so none of them is kept. They are a real
actor's performance: nothing is cloned, remixed or derived from them for shipping.

### Her sounds

The inventory reads the installed Awakening content: sound declarations,
including the entries `valkaryne.sndshd` declares without the `sound` keyword;
her definition's `snd_*` keys and the frame commands that play them; her
effects; and every speaker, radio call and script that plays a line.

**Voice: 37 lines, all recorded, all re-voiced.** Every one is listed under
[the lines](#awakening-valkaryne-re-voiced-37-clips-6-sound-effects-723-characters)
with its synthesis text, preset and where it plays. In summary:

| Where | Lines | How they play | Preset |
| --- | --- | --- | --- |
| m08 Cryofac, the core monitor | `vo_valk_iseeyou` | Radio on the monitor (`$valkaryne_voice`) in `CoreMonitor` | monitor |
| m08 Cryofac, Tak's death | `vo_valk_reborn`, `_stroggetern`, `_makrons`, `_immort`, `_racedies` | Radio on the monitor in `FinalValkaryne` | monitor |
| m08, placed nowhere | `vo_valk_timedie`, `vo_valk_youcntstop` | None: the monitor scene holds the screen 9 s, and the two need 13.9 s ([Cryofac, not placed](awakening-missions.md#not-placed)) | monitor (optional) |
| m09, through the level | `vo_valk_useless` to `vo_valk_youhum2` (`valk_speaker_1`-`_14`) | Global speakers fired by the level's triggers and scripts | nexus |
| m09, the boss fight | `vo_valk_youwillserv2` to `vo_valk_youpowstrgg` (`valk_speaker_15`-`_26`) | Revision 5's `openq4_valkTaunts` | nexus |
| m09, the boss's own | `vo_valk_advanevo` (intro), `_etern2` (undock), `_impstrgg2` (death) | `valkIntro`, `valkaryne_undock`, `valkDead` | nexus |

Lines spoken for the hive ("our", "we", "the Strogg", "All is Strogg") carry
`hive`, and the processing adds the collective to them. Two pairs share their
words, so each line is generated separately: `vo_valk_iseeyou` and
`vo_valk_timedie` for the monitors, and `_iseeyou2` and `_timedie2` for the
finale.

**Creature vocalisations: six candidates (optional).** These are sound effects,
not speech. Listen to each original before replacing it; nothing here has been
heard.

| Original | Plays as | Plays when |
| --- | --- | --- |
| `valkaryne_idle_breath.ogg` | `snd_idle`, `snd_plugin`, `snd_crouch` | Idle, docked, crouching |
| `valkaryne_pain_one` to `_four.ogg` | `snd_pain` | `pain1`-`pain6`, the docked flinches |
| `valkaryne_death.ogg` | `snd_death` | `death` |

**Body and weapons: kept.** These are machine sounds already, and nothing in
the request asks for them. They are footsteps (`snd_step`), walking, running
and turning servos, docking (`plugin`, `plugintoidle`, `runtoplugin`), evades,
the leans, melee and leap attacks, and weapon charges and shots
(`valkaryne_plugin_range`, `_plugin_range_big`, `_range_one`/`_two`, whose
files also serve `snd_range_fire_one`/`_two`). Also kept are the shockwave and
projectile spread, which belong to the cut attack. Her melee, leap and lean
sounds may be vocal efforts; listen before deciding. Her effects borrow the
Tank's railgun sounds (`tank_railgun`, `tank_chargerailgun`) and impact and
explosion sounds from the Gladiator and the player's weapons. Thirteen more
`valkaryne_*` declarations (machine gun, nail gun, missiles, growl, breathe,
swing) name files that were never made, and nothing plays them.

### The voice

The profile is `voices.valkaryne` in the line list:

- **Description** (499 characters, Voice Design's limit is 500): "A soulless
  machine intelligence speaking through a cybernetic war body; nothing human
  remains. Heavily synthesized, robotic voice: vocoded, metallic and resonant,
  flat controlled pitch, no breath, warmth or emotion. Low layered tones with a
  cold digital edge and faint electronic distortion. Slow, deliberate delivery;
  every word clipped and final. Unnerving, menacing calm, like a hive mind
  passing judgment. Feminine only in contour, never in warmth. Clean recording,
  no music or background noise."
- **Preview text:** original lines in her register, about 270 characters.
- **Models:** `eleven_ttv_v3` to design, `eleven_v3` to speak,
  `eleven_text_to_sound_v2` for the effects. Speech uses stability 0.7,
  similarity 0.8, style 0 and speed 0.85, for a slow and even delivery.
  `vo_valk_laff2` uses v3's `[menacing laughter]` tag; if a take comes out
  wrong, generate the laugh as a sound effect instead.

The character brief behind the description is her role in the content: the
Makron Guardian and data conduit, a pale human face sealed into a crowned war
machine. Her recordings were analysed for this brief on 28 November.

### Generating

`tools/assets/elevenlabs_voice.py` runs the whole pipeline. It reads the key
from `ELEVENLABS_API_KEY` or, failing that, from a file holding only the key:
`~/.elevenlabs/api_key`, outside the repository (`ELEVENLABS_API_KEY_FILE` names
another path). You set the key yourself, and the tool never prints it. It writes
only under `.tmp/voice/valkaryne/`. Voice ids are kept there too, because they
belong to the account. A voice already designed in the ElevenLabs app skips
steps 1 and 2: record its id as `{"voice_id": "..."}` in `voice.json`, as was
done for the Valkaryne's voice on 28 November.

1. `design`: about three previews from the description (`design/*.mp3`,
   recorded with their `generated_voice_id`). Listen, and repeat with
   `--seed N` until one is right.
2. `create --preview <generated_voice_id>` saves it as a voice in the account.
3. `speak --takes 3 [--sfx]` writes three takes of each line (`dry/<id>.takeN.wav`).
   It requests 24 kHz PCM, which every plan has, and resamples to the game's
   44.1 kHz; `--format pcm_44100` needs Pro. Sound effects are requested as WAV,
   because their PCM is stereo. `--dry-run` prints the text and the character
   count without a request.
4. `process --all-takes` processes every take into `processed/takes/`, so takes
   are compared as they will sound. Write the choices in `picks.json`
   (`{"vo_valk_useless": 2, ...}`), then run `process` (`processed/<id>.wav`).

**Cost.** 723 characters × 3 takes is 2,169 characters for the speech. Add the
Voice Design previews, and, with `--sfx`, six sound effects × 3, billed by the
account's sound-effect pricing. Check the plan's credits first (see
[Cost](#cost)).

**Processing** (the `process` command). Each step is tied to her design or to
her original recordings:

| Step | Setting | Why |
| --- | --- | --- |
| Lowered core | −5 semitones | Her finale takes sit 4-7 semitones under her spoken ones. |
| Vocoder, about 70% of the mix | 28 bands from 80 Hz to 9 kHz; carrier a sawtooth root, fifth and octave following her pitch lowered 5 semitones with 35% of its movement, plus noise above 3 kHz | Synthesised and flat: "feminine only in contour". |
| Sub-octave | −12 semitones, at 0.4 | Weight and menace. |
| Ring modulation | 45 Hz, 30% depth, on the core | An electrical buzz under every word. |
| Metal | Comb at 0.84 ms, feedback 0.65 | The one machine trait her alpha takes had (measured), made stronger. |
| Grit | 2-8 kHz band crushed to 6 bits, held 4 samples, at 0.12 | Nexus data. |
| Hive (hive lines) | Three copies at +15, −20 and +25 cents, delayed 12, 19 and 27 ms | The Strogg speak through her. |
| Space | nexus: dark 1.3 s tail, 28% wet. monitor: 300 Hz-3.5 kHz speaker and a 0.3 s room | Global voice; Cryofac's screens. |
| Level | About −13 dBFS while speaking, soft ceiling −0.5 dBFS | Her originals' levels. |

The `creature` preset keeps the metal, the ring modulation and the grit, with
no vocoder, for the sound effects.

**Checked so far (no key).** `list` and `speak --dry-run` read all 43 entries.
`process --input` on five of the alpha's takes (`behold`, `youwillserv2`,
`impstrgg2`, `laff2` and the monitor line `racedies`) writes 44.1 kHz mono WAVs
to `.tmp/voice/valkaryne/audition/`, for a local listen only:

- the comb rises from 8 to 21-22 dB at 0.84 ms;
- the voice centres 4-7 semitones lower, and the sub-octave rises from −25 to
  −31 dB up to −10 to −17 dB;
- the laugh's pitch movement flattens to 1.5 semitones;
- speech lands at −13.1 to −13.4 dBFS (the monitor line at −14.7, after its
  band), with peaks −1.1 to −2.2 dBFS.

**Generated, 28 November 2026.** The user designed the voice in the
ElevenLabs app. A three-line pilot was approved, and then `speak --takes 3
--sfx` produced 111 speech takes (`eleven_v3`, 2,169 characters) and 18
sound-effect takes. None was silent, and every speech take runs 0.64-4.40 s.

- **Her voice is synthetic before any processing:** the raw takes sit at
  117-165 Hz and already carry a 14-18 dB comb at about 0.86 ms.
- **Effect PCM is stereo.** The effects first came back twice their length,
  because their headerless PCM is stereo and was read as mono. The received
  streams were de-interleaved and averaged, which restored 1, 3 and 4 s, and
  the tool now asks for WAV.
- **Processed takes** measure −14.7 to −12.5 dBFS while speaking, with peaks
  −2.2 to −0.9 dBFS. Every finale line fits the 7-second taunt slot.
- **Cryofac's monitor lines** come within 0.7 s of the originals.
  `vo_valk_timedie` and `vo_valk_youcntstop` now total 3.5 s against 13.9 s,
  which would fit the 9-second monitor scene that could not hold them.
- **The idle breath** loops in the game, so it must go in without the reverb
  tail and trim the processing adds.

The takes stay under `.tmp/voice/valkaryne/`; `install` copies the chosen
ones into the game (below). Only the pilot has been listened to, by the user.

### In the game (28 November 2026)

`install` copies the processed takes into `pak0` and writes their
declarations; the game swaps them in.

- **Audio.** 37 lines go under `sound/vo_openq4/valkaryne/`. A `sound/vo_<name>/`
  folder is the engine's dialogue path, so the lines get captions and follow
  Dialogue Volume, and the `sound/vo/` language remap leaves the folder alone.
  The four pains and the death go under `sound/openq4/valkaryne/`. Together
  they add about 8 MB to `pak0`, from 17.2 to 25.5 MB, which every package and
  dedicated server carries; moving them to a client-only pack would need a
  content generation.
- **Declarations.** `sound/openq4_valkaryne.sndshd` declares `oq4_<name>` for
  each of her 37 line shaders, `valkaryne_pain` (four takes) and
  `valkaryne_death`, keeping the original's distances and volume.
  `lipsync/openq4_valkaryne.lipsync` gives each line an authored caption,
  `subtitle/oq4_<shader>`, with the original line's text, the laugh included.
- **The swap** (`ContentRepairs.inl`, "The Valkaryne's voice") runs while
  `g_awakeningValkaryneVoice` is 1, the saved default. It changes a name only
  when its `oq4_` declaration exists, and it does not depend on
  `g_awakeningContentRepairs`. Her sounds reach the game three ways, and each
  asks the swap:
  - a speaker's `s_shader`, at spawn and media precache;
  - her definition's `snd_*` keys, set on her map entities, which the
    definition's defaults never override;
  - a radio call (`Awakening_RecordedLine`), as it plays.
- **Not swapped.** Her idle breath stays the alpha's. Her docked idle is a
  2-frame animation that restarts `snd_plugin` every 350 ms (measured), so a
  4-second breath would be cut to its first third of a second. A breath worth
  replacing needs its own looping channel. Her body and weapon sounds stay too
  (see above).

Validation, on a hidden Windows x64 runtime built from this tree (the client
with the new `pak0`'s digest), sound device at zero volume, `s_showStartSound`
and `s_logSpeech`:

| Probe | Result |
| --- | --- |
| `m09swap_1` / `m09swap_cap` | The intro, a taunt, the laugh, undock and death lines play `oq4_vo_valk_*` from `sound/vo_openq4/`, classed as dialogue with authored captions; her pains and death play `oq4_valkaryne_pain`/`_death`. |
| `m09swap_0` | `g_awakeningValkaryneVoice 0`: the same moments play the alpha's `vo_valk_*`, `valkaryne_pain` and `valkaryne_death`. |
| `m08radio` | Cryofac's `FinalValkaryne` plays `oq4_vo_valk_reborn`, `_stroggetern`, `_makrons`, `_immort` and `_racedies` over the radio, as dialogue with captions. |
| `m09vsave` / `m09vload` | A save restored in a fresh process: a speaker still plays its new line, with its caption. |

`tools/tests/voice_generation_lines.py` checks the installed WAVs (44.1 kHz,
mono, 16-bit), that each `oq4_` declaration names exactly its installed takes,
a caption for every line, and that the swap runs before the content-repair
gate. Three negative controls were each rejected.

**Limits.**

- Only the three pilot lines have been heard, by the user; the full set has
  been measured, not listened to.
- The takes are each line's take 0 until `picks.json` changes them; then run
  `process` and `install`, and rebuild `pak0`.
- A save keeps the speakers and her keys as they were when its level started.
- **Usage rights.** Whether generated audio may be distributed depends on the
  ElevenLabs plan it was made on. ElevenLabs grants commercial use on paid
  plans; the free plan requires attribution. Confirm the plan before
  publishing.
- The setting is console-only, like the other Awakening options.
- Vulkan, Linux and macOS were not run.

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

#### Awakening Valkaryne re-voiced (37 clips, 6 sound effects, 723 characters)

| ID | Voice | Text | Notes |
| --- | --- | --- | --- |
| `vo_valk_useless` | valkaryne | Useless. | nexus; required; original 0.93 s; m09: speaker valk_speaker_1 <- moveIceCubeDown |
| `vo_valk_nohope` | valkaryne | There is no hope for you. | nexus; required; original 1.82 s; m09: speaker valk_speaker_2 <- trigger_once_10 |
| `vo_valk_racewill` | valkaryne | Your race will become ours. | nexus, hive; required; original 1.86 s; m09: speaker valk_speaker_3 <- trigger_once_11 |
| `vo_valk_defeat` | valkaryne | Defeat is inevitable. | nexus; required; original 1.4 s; m09: speaker valk_speaker_4 <- trigger_once_68 |
| `vo_valk_yield` | valkaryne | You will yield... or you will die. | nexus; required; original 2.45 s; m09: speaker valk_speaker_5 <- trigger_relay_24 |
| `vo_valk_behold` | valkaryne | Behold our perfection. | nexus, hive; required; original 1.74 s; m09: speaker valk_speaker_6 <- trigger_relay_25 |
| `vo_valk_allis` | valkaryne | All is Strogg. | nexus, hive; required; original 1.77 s; m09: speaker valk_speaker_7 <- trigger_once_70 |
| `vo_valk_deathserv` | valkaryne | In death, you will serve. | nexus; required; original 1.79 s; m09: speaker valk_speaker_8 <- trigger_once_71 |
| `vo_valk_makronrs` | valkaryne | The Makron will rise. | nexus; required; original 1.93 s; m09: speaker valk_speaker_9 <- trigger_once_15 |
| `vo_valk_iseeyou2` | valkaryne | I see you. | nexus; required; original 4.92 s; m09: speaker valk_speaker_10 <- trigger_once_16 |
| `vo_valk_timedie2` | valkaryne | Time to die. | nexus; required; original 4.76 s; m09: speaker valk_speaker_11 <- trigger_once_72 |
| `vo_valk_toolate2` | valkaryne | Too late. | nexus; required; original 3.67 s; m09: speaker valk_speaker_12 <- trigger_once_73 |
| `vo_valk_iamthestrg2` | valkaryne | I am... the Strogg. | nexus, hive; required; original 5.41 s; m09: speaker valk_speaker_13 <- trigger_once_30 |
| `vo_valk_youhum2` | valkaryne | You are human. | nexus; required; original 4.71 s; m09: speaker valk_speaker_14 <- trigger_once_19 |
| `vo_valk_youwillserv2` | valkaryne | You will serve me. | nexus; required; original 5.13 s; m09: speaker valk_speaker_15 <- openq4_valkTaunts (M09 revision 5) |
| `vo_valk_laff2` | valkaryne | [menacing laughter] Ha... ha... ha... ha. | nexus; required; original 5.32 s; m09: speaker valk_speaker_16 <- openq4_valkTaunts (M09 revision 5) |
| `vo_valk_witnesso2` | valkaryne | Witness... your oblivion. | nexus; required; original 5.78 s; m09: speaker valk_speaker_17 <- openq4_valkTaunts (M09 revision 5) |
| `vo_valk_behold2` | valkaryne | Behold... Strogg glory. | nexus, hive; required; original 5.55 s; m09: speaker valk_speaker_18 <- openq4_valkTaunts (M09 revision 5) |
| `vo_valk_persist2` | valkaryne | Your persistence... is your weakness. | nexus; required; original 6.41 s; m09: speaker valk_speaker_19 <- openq4_valkTaunts (M09 revision 5) |
| `vo_valk_suffer2` | valkaryne | Suffer. | nexus; required; original 3.55 s; m09: speaker valk_speaker_20 <- openq4_valkTaunts (M09 revision 5) |
| `vo_valk_pain2` | valkaryne | Pain. | nexus; required; original 2.9 s; m09: speaker valk_speaker_21 <- openq4_valkTaunts (M09 revision 5) |
| `vo_valk_conq2` | valkaryne | Conquest. | nexus; required; original 3.48 s; m09: speaker valk_speaker_22 <- openq4_valkTaunts (M09 revision 5) |
| `vo_valk_human2` | valkaryne | Humanity... is mine. | nexus; required; original 5.22 s; m09: speaker valk_speaker_23 <- openq4_valkTaunts (M09 revision 5) |
| `vo_valk_power2` | valkaryne | My power... is beyond limits. | nexus; required; original 5.64 s; m09: speaker valk_speaker_24 <- openq4_valkTaunts (M09 revision 5) |
| `vo_valk_yousee2` | valkaryne | You see my power. | nexus; required; original 5.43 s; m09: speaker valk_speaker_25 <- openq4_valkTaunts (M09 revision 5) |
| `vo_valk_youpowstrgg` | valkaryne | You see the power... of the Strogg. | nexus, hive; required; original 6.22 s; m09: speaker valk_speaker_26 <- openq4_valkTaunts (M09 revision 5) |
| `vo_valk_advanevo` | valkaryne | Advancement... Evolution... Perfection. | nexus; required; original 8.03 s; m09: speaker valk_speaker_27 <- valkIntro |
| `vo_valk_etern2` | valkaryne | Eternal. | nexus; required; original 3.69 s; m09: speaker valk_speaker_28 <- valkaryne_undock |
| `vo_valk_impstrgg2` | valkaryne | Impossible... We are Strogg. | nexus, hive; required; original 6.92 s; m09: speaker valk_speaker_29 <- valkDead |
| `vo_valk_iseeyou` | valkaryne | I see you. | monitor; required; original 3.81 s; m08: monitor/radio $valkaryne_voice <- CoreMonitor |
| `vo_valk_timedie` | valkaryne | Time to die. | monitor; optional; original 4.88 s; m08: recorded, placed nowhere (the monitor scene holds the screen 9 s; with vo_valk_timedie this needs 13.9 s) |
| `vo_valk_youcntstop` | valkaryne | You cannot stop us. | monitor, hive; optional; original 5.2 s; m08: recorded, placed nowhere (the monitor scene holds the screen 9 s; with vo_valk_timedie this needs 13.9 s) |
| `vo_valk_reborn` | valkaryne | Reborn. | monitor; required; original 1.21 s; m08: monitor/radio $valkaryne_voice <- FinalValkaryne |
| `vo_valk_stroggetern` | valkaryne | The Strogg are eternal. | monitor, hive; required; original 1.6 s; m08: monitor/radio $valkaryne_voice <- FinalValkaryne |
| `vo_valk_makrons` | valkaryne | The Makrons... | monitor; required; original 1.1 s; m08: monitor/radio $valkaryne_voice <- FinalValkaryne |
| `vo_valk_immort` | valkaryne | ...are immortal. | monitor, hive; required; original 0.93 s; m08: monitor/radio $valkaryne_voice <- FinalValkaryne |
| `vo_valk_racedies` | valkaryne | Your race dies here. | monitor; required; original 1.53 s; m08: monitor/radio $valkaryne_voice <- FinalValkaryne |
| `valkaryne_idle_breath` | valkaryne (sound effect) | Slow mechanical breathing of a female cyborg war machine: hydraulic intake, hissing exhale, low servo hum, synthetic and cold, seamless loop. | sound effect, 4 s, loop; vocalisation for snd_idle, snd_plugin, snd_crouch; replaces sound/strogg/valkaryne/valkaryne_idle_breath.ogg; listen to the original first |
| `valkaryne_pain_one` | valkaryne (sound effect) | Short distorted shriek of a synthetic female cyborg in pain, metallic, electronic glitch, no human breath. | sound effect, 1 s; vocalisation for snd_pain (pain1-6, plugin_pain1/2); replaces sound/strogg/valkaryne/valkaryne_pain_one.ogg; listen to the original first |
| `valkaryne_pain_two` | valkaryne (sound effect) | Sharp mechanical female screech of pain, vocoded and metallic, abrupt. | sound effect, 1 s; vocalisation for snd_pain; replaces sound/strogg/valkaryne/valkaryne_pain_two.ogg; listen to the original first |
| `valkaryne_pain_three` | valkaryne (sound effect) | Guttural synthetic female growl of pain with digital distortion and servo whine. | sound effect, 1 s; vocalisation for snd_pain; replaces sound/strogg/valkaryne/valkaryne_pain_three.ogg; listen to the original first |
| `valkaryne_pain_four` | valkaryne (sound effect) | Electronic female cry of pain breaking into static, metallic resonance. | sound effect, 1 s; vocalisation for snd_pain; replaces sound/strogg/valkaryne/valkaryne_pain_four.ogg; listen to the original first |
| `valkaryne_death` | valkaryne (sound effect) | Dying female cyborg machine: a long vocoded scream collapsing into electronic static and a falling power-down whine. | sound effect, 3 s; vocalisation for snd_death; replaces sound/strogg/valkaryne/valkaryne_death.ogg; listen to the original first |
