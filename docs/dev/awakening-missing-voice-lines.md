# Awakening missing voice lines register

Every voice line in The Awakening that names something missing from the alpha
content: which pieces are missing, whether the player can reach the line, and
how openQ4 resolves it. Lines whose name was only misspelt and whose recording
exists are not listed: `src/game/awakening/ContentRepairs.inl` restores those
(see [Silent names](awakening-missions.md#silent-names--14-november-2026)).

Checked on 16 November 2026 against the installed Awakening content
(`q4xbase`) and the retail Quake 4 assets. Every recorded line of both games
(about 9,100 with text) was searched for the words of each missing line.

## Status

| Group | Lines | Reachable in play | Resolution |
| --- | --- | --- | --- |
| m05 Pain Lord tube marines' screams | 3 | yes | **Resolved**: stand-in screams from existing recordings |
| m08 Darnell's radio intro | 2 | yes | **Open, recorded**: the samples exist without declarations; the alpha's beep removed |
| m01 "+use" replies | 23 | yes | **Open, recorded**: a key slip plays the missing-sample tone |
| m01 battle radio (draft) | 29 | placed | **Placed; audio open**: hooked to the shipped battle's scenes, silent until the lines are voiced |
| m01 draft scenes the maps do not have | 100 | no | **Open**: the scene does not exist in the shipped mission |
| m01 Darnell and Tak trench lines | 2 | no | **Closed**: declared, called by nothing |
| m06 empty `vo_` entries | 5 | no | **Closed**: duplicates of recorded lines that play |

Where the words are known, the line is to be voiced and played. The m01 draft
is the one source that needs new audio: its radio traffic is placed in the
shipped battle and waits only for its recordings (see
[m01 battle radio](#m01-battle-radio-29-placed-audio-open)). The m08 pair has no
words anywhere, so there is nothing to voice: its two lines turn out to be recorded.

A missing sample is not silent in this engine: the sound system fills it with a
generated 186 ms tone, so every unrecorded line that is called beeps. The
alpha beeped where the tube marines should scream, at the end of Darnell's
radio intro and on every m01 "+use" reply. The first two are fixed; the third
is open. Everything that would need ElevenLabs is in the
[voice generation breakdown](voice-generation-breakdown.md).

## The pieces of a line

A line plays when every piece in its chain exists:

| Piece | Where it lives | What it does |
| --- | --- | --- |
| Speaker key | entityDef or map entity, `"lipsync_<name>" "vo_<name>"` | Lets `aiSpeak` find the line for that character. Radio calls skip this. |
| Sound declaration | `sound/*_vo.sndshd`, `sound vo_<name>` | Names the sample to play. |
| Sample | `sound/vo_english/<mission>/<name>.wav` | The recording. |
| Lip-sync declaration | `lipsync/*_vo.lipsync`, `lipSync vo_<name>` | The caption text and the `visemes` mouth shapes. Needed only by a speaker with a face. |
| Writers' text | `scripts/maps/*_vo.script` (unloaded), or the sound's `description` | The intended words. The game does not load it; it is the evidence of what the line was. |

Key: **yes** present, **no** missing, **n/a** not needed for that call.

## Reachable lines

### m05 Bio, Pain Lord tube marines (3): resolved

`aiSpeak` calls in `scripts/maps/m05_painlord.script`, one as the machine
processes each marine. Everything is declared except the recording.

| Line | Script line | Speaker | Text | Speaker key | Sound decl | Sample | Lip-sync | Visemes | Plays now |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `vo_marine1_agony1` | 102 | `pl_tubemarine_1` | "(Screams of agony)" | yes (`def/levels/m05_bio.def`) | yes (`m05_bio_vo.sndshd`) | **no** (`sound/vo/M05_Bio/marine1_agony1.wav`) | yes (`m05_bio_vo.lipsync`) | no | `sound/vo/m05_bio/scrmar4_pain.wav` |
| `vo_marine2_agony1` | 224 | `pl_tubemarine_2` | "(Screams of agony)" | yes | yes | **no** (`.../marine2_agony1.wav`) | yes | no | `sound/vo/medlabs_1/2_1_1_01_5.ogg` |
| `vo_marine3_agony1` | 249 | `pl_tubemarine_3` | "(Screams of agony)" | yes | yes | **no** (`.../marine3_agony1.wav`) | yes | no | `sound/vo/medlabs_1/2_1_1_01_7.ogg` |

The screams have no words, so a stand-in take loses nothing the writers
specified. The first is the expansion's own unused take of a screaming marine
(`vo_scrmar4_pain`, "(Screams in pain)", 4.9 s). The other two are retail Quake
4's Stroggification screams from the medical labs (`vo_2_1_1_01_1`, "(Screams of
pain for varying durations)", 5.3 s and 5.5 s), the same kind of scene, so the
three men do not share one voice. The rows are in the `awakeningSpeech` table:
each line starts under its own lip-sync declaration, so its caption shows, and
plays the stand-in by path. Content that supplies the missing recordings takes
over.

### m08 Cryofac, Darnell's radio intro (2): open, recorded

The last two calls of `DarnellRadioIntro()` in `maps/game/m08_cryofac.script`.

| Line | Script line | Speaker | Writers' text | Speaker key | Sound decl | Sample | Lip-sync | Visemes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `lipsync_nava_herightsrg` | 303 | `$navajas` (`char_marine_npc_navajas_cryofac`) | no | no | no | **yes** (`sound/vo_english/m08_cryo/nava_herightsrg.wav`, 1.10 s) | no | no |
| `vo_darn_dntwrryct` | 304 | `$func_radiochatter_1` (radio) | no | n/a | no | **yes** (`sound/vo_english/m08_cryo/darn_dntwrryct.wav`, 1.75 s) | no | no |

Both lines are recorded; only their declarations, key and captions are
missing, and the writers' dialogue list never had them. (An earlier version of
this register called them never written: the word search covered declared
lines only.) Their words are inferred from the names, "He's right, Sarge"
and "Don't worry...", and need listening for the captions.

Until they are restored: Navajas's call does nothing (`aiSpeakWait` returns at
once on the missing key), and Darnell's radio call used to play the engine's
186 ms missing-sample tone. `Awakening_UnwrittenLine` keeps a radio line that
nothing declares silent (no radio icon, no tone); `g_awakeningContentRepairs 0`
restores the alpha's beep. **Fix:** play both recordings by path from the
repairs table, under captions written by ear. No generation is needed.

### m01 "+use" replies (23): open, recorded, a key slip

`scripts/maps/m04_stranarus_trench.script` (lines 1178-1185, 1342-1344,
1400-1422 and 2459-2467) sets the squad's talk keys to the names of other keys
(`$darnell.setKey( "lipsync_talk_primary", "lipsync_darn_theywait" )`) where
retail sets sound names (`"lipsync_talk_primary" "vo_1_1_1_46_1"`).
`idAI::TalkTo` speaks the talk key, and `idAI::Speak` looks it up once, so
each reply asks for a sound named `lipsync_darn_theywait`, which does not
exist, and plays the missing-sample tone. All 23 are recorded (1.3 to 4.3 s):
Darnell 5, Tak 5, a Hades marine 3, Markswell 3, Mitchell 3, Navajas 4.
**Fix:** resolve the extra level in the repairs for the m01 maps. No
generation is needed.

### Recorded but placed nowhere

Not gaps in play, and none needs generation; each is a placement question.

- **m02 later "+use" replies (14):** Navajas 3, Markswell 3, Tak 8, all
  recorded (33.5 s), keyed only in the unused `char_npc_*_m02_trian__vo`
  definitions.
- **m07 race radio (45):** recorded under `sound/vo_english/m08_race/`
  (`8_1_*` to `8_4_1`, 2.2 minutes), captions in `lipsync/m08_race_vo.lipsync`,
  no sound declarations. The race's chatter script is switched off.
- **Unreferenced recordings (922, about 48 minutes):** reached by no
  declaration, map or script, mostly in folders of the expansion's older
  mission numbering (`m01_airassault` 251, `m07_mcc_invasion` 141,
  `m03_mccfirst` 106, `m05_prison1` 92, `m06_prison2` 83, `m02_trianfac` 74,
  `m04_stranarus_trench` 68, `m10_valkaryne` 28). The 68 in
  `m04_stranarus_trench/` share the m01 draft's numbering; their lengths fit
  some draft lines and not others, so they are a different revision. A
  transcription pass would show which draft lines already exist.

## m01 battle radio (29): placed, audio open

### Where the lines come from

`scripts/maps/m04_stranarus_trench_intro.script` is an early draft of the trench
mission, from when it was mission 4 and Viper squad's. Its header calls it a
"Brief document of all the new radio chatter to be used in both levels".
`scripts/main.script` compiles it, but no map calls into its namespace
(`m04_stranarus_trench_intro`): both m01 maps call `m04_stranarus_trench::main`,
and neither has its radio entity (`trench_sound_chatter_1`) or the marines it
directs. Its words survive in the unloaded
`scripts/maps/m04_stranarus_trench_vo.script`: 129 lines, none recorded or
declared. Every piece but the writers' text is missing for all of them.

The draft's radio traffic is the part the shipped battle can carry: it is the
war going on around Falcon squad, reported by other squads and by Markswell.
29 lines qualify, the 20 the draft script calls over its radio and nine more
the writers tagged "Over Radio".

### Placement

`scripts/openq4/awakening/m01_radio.script` (openQ4 content, revision 1) holds
one function per scene. `src/game/awakening/M01Radio.inl` points the matching
trigger of the shipped map at it; the function runs the map's own scene
unchanged and then its radio. A line waits until the shared radio
(`uni_radiochatter`) has been quiet for a moment, so the story's calls are
never talked over, and a story call that starts during one takes the radio.
Markswell's calls stop once the player is with him, and his repeated order only
plays if the player has not gone into the tunnels a minute later.

| Map | Shipped scene (trigger) | What happens there | Lines, in order |
| --- | --- | --- | --- |
| trench1 | `m04_trench_unit1_seq_2` | The landing zone: "We landed in a real mess", the squads torn up by turrets | `vo_4_2_2_3` "Objective is lost, Command, please advise!", `vo_4_2_2_4` "Oh fuck! They're already here!", `vo_4_2_2_5` "I need an evac now...", `vo_4_2_2_6` "We've lost contact with Raptor and Venom squads.", `vo_4_2_2_1` "They're all over us! Oh god -", `vo_4_2_2_2` "It's no good, Command..." (the draft's second wave on the beach, in its order and spacing) |
| trench1 | `m04_trench_unit1_seq_3` | The Harvester falls; Razor 16 is hit and goes down; air support lost | `vo_4_1_10` "We're taking heavy fire, we've lost two port thrusters, and.", `vo_4_2_2_13` "This isn't an assault, Command. It's a fucking massacre.", `vo_4_2_2_12` "They won't stop coming!..." |
| trench1 | `m04_trench_unit1_seq_4` | The walkers break for the tunnels; Tak and Darnell move the squad up | `vo_4_5_1` "Tiger squad!", `vo_4_5_2` "They were waiting for us!", `vo_4_5_3` "Tiger squad, fall back. Fall back!", `vo_4_2_2_7` "Raptor squad is down...", `vo_4_2_2_8` "Command, where is our fucking support?" |
| trench1 | `m04_trench_unit1_seq_7` | The push on the turret bunkers; Markswell waits ahead | `vo_4_2_11` Markswell "Get moving, Marine.", `vo_4_2_2_11` "Watch the turrets! Watch the--", `vo_4_2_12` Markswell "Breach those bunkers, Sergeant." |
| trench1 | `m04_trench_unit1_seq_7_player_blocker_1_trig` | Hades squad sends Falcon on to the rest of the squad | `vo_4_2_2_14` "Bunker eight-nine-bravo is clear!" |
| trench1 | `m04_trench_unit1_seq_9` | Markswell's orders: breach the supply tunnels, "the only way we're gonna get past these turrets" | `vo_4_2_13` Markswell "You have your orders, Sergeant. Find a way behind those turrets.", only if the player has not gone in 60 s later |
| trench1 | `m04_trench_unit1_seq_10_begin_run_inside` | The cave mouth comes down behind the squad (the draft's avalanche) | `vo_4_5_4` "What the hell is that?", `vo_4_5_5` "Oh shit!", `vo_4_2_2_9` "Eastern approach is stalled..." |
| trench2 | `m04_building_unit2_seq_1` | Out of the building, on to the supply tunnels | `vo_4_2_2_15` "We've breached the Northern wall...", `vo_4_2_2_16` "Mongoose squad is in position..." |
| trench2 | `m04_maint_tunnel_unit1_seq_1` | The damaged walkers found | `vo_4_2_2_17` "(Cough) Command (Cough) Sector two-four-nine is lost.", `vo_4_2_2_18` "It's a clusterfuck, Command..." |
| trench2 | `m04_maint_tunnel_unit1_seq_4_area_clear_and_charges_set` | The first tunnel cleared and its charges set | `vo_4_2_2_10` "Objective secure." |
| trench2 | `m04_trench_unit2_seq_1` | Back in the trench for the last turrets | `vo_4_2_2_19` "They're all dead!... (Gunshot)", `vo_4_2_2_20` "Oh shit! They're taking us apart..." |

The placement compiles only on the two trench maps, only when every hooked
trigger still calls the scene it was written for, and (with the default
`g_awakeningM01Radio 1`) only once the content declares the lines; until then
the maps run exactly as before. `g_awakeningM01Radio 2` runs it without audio,
to test the timing: each line is logged, and a line nothing declares stays
silent (no radio icon, no missing-sample tone). `0` turns it off.

### Audio still to make

For each of the 29 lines: a sample, a sound declaration under the draft's own
name (`vo_4_*`), and a lip-sync declaration carrying its caption. The radio
repairs pick them up unchanged. Voices:

- Anonymous squads (25 lines: marines 1 to 12, Tiger squad's three): one stock
  voice per speaker, kept for all of that speaker's lines, processed to match
  the battle's recorded radio traffic.
- Markswell (3 lines, all over the radio): `vo_4_2_11` has
  his recorded take of the same words (`vo_marks_getmov`, "Get moving
  marine!"), `vo_4_2_13` a close one (`vo_marks_youhaveords`, "You have your
  orders marine!"). `vo_4_2_12` has none; voicing it in his voice means cloning
  a real actor's performance.

### Not placed: no scene in the shipped mission (100)

The rest of the draft is spoken face to face in scenes the shipped maps do not
have: Viper squad's own dropship and crash, Tak's uplink, Trager's demolition
stand, the trapped squads Dekard reaches, the prison wing. Each needs a scene
before it needs a voice. `vo_4_6_1` (Markswell's turret warning) is superseded
by his recorded `vo_marks_dektkoutturr` ("Dekard, take out those turrets!") at
the same turret.

| Draft scene | Lines | Line | Writers' text |
| --- | --- | --- | --- |
| Viper squad's dropship briefing and crash (Markswell commanding Viper squad, not Falcon) | 6 | `vo_4_1_1` | markswell: Viper Squad, our mission is simple. Get in; Get Strauss; Get out. I'm not going to lie to you gentlemen, it's going to be nasty. Give'em hell, Marines. |
|  |  | `vo_4_1_7` | viper squad: Sir. Yes sir. |
|  |  | `vo_4_1_8` | markswell: We're about thirty seconds from the infiltration point. Feel free to -- |
|  |  | `vo_4_1_9` | markswell: What the hell? Damage report! |
|  |  | `vo_4_1_11` | markswell: We're at the infiltration point, get - |
|  |  | `vo_4_1_12` | markswell: Oh Shit! Viper Squad. Prepare of impact. Prepare for im - |
| The crash site and the beach: a medic over the Sergeant, ambient marines' barks | 10 | `vo_4_2_1` | marine8: (Ser)geant? Sergeant? You're gonna be fine, Sir. |
|  |  | `vo_4_2_2` | marine11: Sir, breach point's ahead. Stay low and go! |
|  |  | `vo_4_2_3` | marine1 : Get down. |
|  |  | `vo_4_2_4` | marine2: Watch yourself. |
|  |  | `vo_4_2_5` | marine3: We're taking heavy fire. |
|  |  | `vo_4_2_6` | marine4: Move ahead, Sir. |
|  |  | `vo_4_2_7` | marine5: Two more squads touched down, west side. |
|  |  | `vo_4_2_8` | marine6: Stay down. |
|  |  | `vo_4_2_9` | marine7: Careful, Sir. |
|  |  | `vo_4_2_10` | marine8: Stay behind cover. |
| Tak breaches the wall | 3 | `vo_4_3_1` | tak: Setting charges. |
|  |  | `vo_4_3_2` | tak : Stand back. |
|  |  | `vo_4_3_3` | tak: Clear! |
| Tak's uplink spike into the facility's transport hub | 6 | `vo_4_4_1` | tak: Sergeant? Tiger squad's already entered the facility's transport hub - It's a massacre, Sir. We've got to get them some backup |
|  |  | `vo_4_4_2` | tak<Over Radio>: Setting spike. |
|  |  | `vo_4_4_3` | tak: Establishing uplink. |
|  |  | `vo_4_4_4` | tak: Hold on, we're hacking now. |
|  |  | `vo_4_4_5` | darnell: We got incoming! Keep'em off Tak! |
|  |  | `vo_4_4_6` | darnell: That's it, we're in. |
| Asking to rescue Tiger squad | 2 | `vo_4_5_6` | marine7: Sir? We've got to help the rest of my squad, the damn squibs have cut'em off from the main assault force. Please, Sir. |
|  |  | `vo_4_5_7` | marine12: They're good men, Sir. We need to try. |
| Markswell's turret warning, and a survivor | 2 | `vo_4_6_1` | markswell<Over Radio>: Sergeant! Watch yourself, there's an automated turret ahead. Armadillo squad took out its shield generators. Finish the job, Marine. |
|  |  | `vo_4_6_2` | marine10: Careful, Sergeant. They. They were ready for us. |
| The trapped squads Dekard reaches: reinforcements, Dodson, Tiger squad's survivors, Barrington | 30 | `vo_4_8_1` | marine8: Men, we got reinforcements. Viper squad made it. |
|  |  | `vo_4_8_2` | marines : (Cheers) |
|  |  | `vo_4_8_3` | marine10: We're taking heavy causalities, Sergeant. I've never seen anything like this, everything's going to shit. |
|  |  | `vo_4_8_4` | marine4: That was amazing, Sir. |
|  |  | `vo_4_8_7` | marine11: I can't believe we made it through there. Half second later and we'd have been trapped. |
|  |  | `vo_4_8_8` | marine3: I don't even wanna think about. |
|  |  | `vo_4_8_9` | marine2: Sir! Have you talked with Dodson? |
|  |  | `vo_4_8_9_1` | marine6: Dekard! I saw the Harvester go down, should'a guessed that was your work. |
|  |  | `vo_4_8_9_2` | marine6: I sent a demolition team out to breach into the facility, but we lost contact. If you can get there, you should be able to blow the door yourself. Good luck, Sergeant. |
|  |  | `vo_4_8_10` | marine8: Man, are we glad to see you. |
|  |  | `vo_4_8_11` | marine8: I can't believe you made it here, you must be one hell of a soldier. |
|  |  | `vo_4_8_12` | marine8: The Strogg have us pinned. With you here, at least we have a chance. |
|  |  | `vo_4_8_13` | marine10: Have you ever seen a mission of this scope? |
|  |  | `vo_4_8_14` | marine10: So many men. I hope its worth it. |
|  |  | `vo_4_8_15` | marine10: I hope the other squads are having better luck. |
|  |  | `vo_4_8_16` | marine4: Hey, you're Sergeant Dekard Chase. Man, the rest of the guys'll never believe I met you. |
|  |  | `vo_4_8_17` | marine4: You think I could get a transfer to Viper Squad? |
|  |  | `vo_4_8_18` | marine4: How did you take down that Harvester? |
|  |  | `vo_4_8_19` | marine6: We need to get into that facility, Sergeant. |
|  |  | `vo_4_8_20` | marine6: The charges should be placed. |
|  |  | `vo_4_8_21` | marine6: You have your orders, Marine. |
|  |  | `vo_4_8_22` | marine11: Thanks for coming, Sir. |
|  |  | `vo_4_8_23` | marine11: The Strogg blew the tunnel just as we got inside. |
|  |  | `vo_4_8_24` | marine11: Have you seen the rest of Tiger Squad? |
|  |  | `vo_4_8_25` | marine3: The tunnel just came down on top of us. |
|  |  | `vo_4_8_26` | marine3: Barrington and I are the only ones that came out. |
|  |  | `vo_4_8_27` | marine3: I hope the others are okay. |
|  |  | `vo_4_8_28` | marine2: Ready when you are, Sir. |
|  |  | `vo_4_8_29` | marine2: It's an honor to serve with you, Sir. |
|  |  | `vo_4_8_30` | marine2: Let's bring it to'em. |
| Trager's demolition stand: the blown fuses, the counter-attack | 9 | `vo_4_9_1` | marine11: Aw shit. The fuses are blown, and the trigger relay is straight up fucked. Its gonna take me a minute to get these ready. |
|  |  | `vo_4_9_2` | darnell: Oh hell, is that what I think it is? |
|  |  | `vo_4_9_3` | tak: We got incoming. |
|  |  | `vo_4_9_4` | dekard: How many? |
|  |  | `vo_4_9_5` | tak: Mother fucking all of them it looks like. |
|  |  | `vo_4_9_6` | darnell: Shit. |
|  |  | `vo_4_9_8` | trager: Watch my back guys. Give me two minutes. |
|  |  | `vo_4_9_9` | tak: Roger that. |
|  |  | `vo_4_9_10` | trager: All right, here we go. Keep'em off me, girls. |
| Trager's three charges under fire; squad callouts and combat barks | 27 | `vo_4_10_1` | trager: First charge is ready. Two more. |
|  |  | `vo_4_10_2` | trager: That's two. |
|  |  | `vo_4_10_3` | trager: That's the third! Clear, clear, clear! |
|  |  | `vo_4_10_4` | trager: Dammit, they're on me! |
|  |  | `vo_4_10_5` | trager: I can't work like this. |
|  |  | `vo_4_10_6` | trager: Keep'em off me! |
|  |  | `vo_4_10_7` | darnell: We lost one! |
|  |  | `vo_4_10_8` | darnell: Man down! |
|  |  | `vo_4_10_9` | darnell: South wall! |
|  |  | `vo_4_10_10` | darnell: Contact, East Side! |
|  |  | `vo_4_10_11` | darnell: Watch the West! |
|  |  | `vo_4_10_12` | darnell: 12 o'clock high, boys! |
|  |  | `vo_4_10_13` | tak: Marine down! |
|  |  | `vo_4_10_14` | tak: We need a medic! |
|  |  | `vo_4_10_15` | tak: They're coming from the South wall. |
|  |  | `vo_4_10_16` | tak: I got Strogg, East side. |
|  |  | `vo_4_10_17` | tak: West side! |
|  |  | `vo_4_10_18` | tak: They're coming in from above! |
|  |  | `vo_4_10_19` | marine1: Contact! |
|  |  | `vo_4_10_20` | marine1: Is that all you bitches got? |
|  |  | `vo_4_10_21` | marine1: Bet'cha didn't count on this, huh, Squibs? |
|  |  | `vo_4_10_22` | marine2: Bring it, Squibs! |
|  |  | `vo_4_10_23` | marine2: They ain't half as tough as they are ugly. |
|  |  | `vo_4_10_24` | marine2: There's more where that came from. |
|  |  | `vo_4_10_25` | marine3: You'll have to do better than that. |
|  |  | `vo_4_10_26` | marine3: Want some of this? |
|  |  | `vo_4_10_27` | marine3: Strogg down. |
| Eagle and Cougar squads wait for a security centre | 1 | `vo_4_11_1` | darnell: Sarge, Eagle and Cougar squads are in place, all we gotta do is find a security center and let'em in. |
| Out of explosives at the door | 1 | `vo_4_12_1` | tak: It's no good, Sergeant. We don't have enough explosives left to blast the door. |
| Markswell sends Eagle and Cougar in; Viper squad to the main prison | 1 | `vo_4_13_1` | markswell<Over Radio>: Good job, Marine. Eagle and Cougar squads are entering now. Meet up with Viper and proceed into the main prison. We have to get Strauss before it's too late. |
| First squad to reach the prison wing | 2 | `vo_4_14_1` | darnell: Sergeant! Glad to see you. We're the first squad to make it this far. |
|  |  | `vo_4_14_2` | tak: We've secured most of the area; all that's left is getting into the main prison wing. |

## Other lines the player cannot reach

### m01 Darnell and Tak (2): closed, called by nothing

| Line | Text | Sound decl | Sample | Lip-sync | Visemes |
| --- | --- | --- | --- | --- | --- |
| `vo_dar_tre_reloading` | Darnell: "They're reloading, go!" | yes (`m01_stranarus_trench_vo.sndshd`) | **no** (`sound/vo/M01_Trench/dar_tre_reloading.wav`) | yes (`m01_stranarus_trench_vo.lipsync`) | no |
| `vo_tak_tre_needocver` | Tak: "Dekard, we need cover!" | yes | **no** (`sound/vo/M01_Trench/tak_tre_needocver.wav`) | yes | no |

No script, map or definition names either line.

### m06 empty entries (5): closed, duplicates

`m06_mcc_vo.sndshd` lines 3223 to 3271 declare five sounds named `vo_` over the
sample `sound/vo/M06_MCC/.wav`: a generator wrote the rows without their names.
Their descriptions are the Dekard and Yoshida exchange ("Stand down Marine." to
"Yes sir"), and those lines are recorded and declared as `vo_7_6_2` to
`vo_7_6_5`, which `maps/game/m06_mcc.map` plays. The five empty rows only
redeclare one another and cost nothing.

## Reproducing

The local audit scripts in `.tmp/awakening-round/` (not tracked) regenerate
this: `voregister.py` prints the per-piece table, `vosearch.py` searches every
recorded line of both games for each missing line's words, and `speakaudit.py
--unresolved` lists calls whose speaker is not a map entity, which is how the
m01 script shows as dead. `tools/tests/awakening_content_repairs.py` proves the
three stand-in rows against the installed content and pins the silent radio
for unwritten lines. In-game probes (OpenGL, hidden, no input, 16 November)
measured the three screams at 4,905, 5,246 and 5,503 ms against the alpha's
185 ms tone, the first two screaming in order as the live fight processes
each marine, and Darnell's unwritten radio line silent against the alpha's
tone, with his recorded line before it still playing. With
`g_awakeningM01Radio 2`, triggering each hooked scene in order logged all 29
placed lines at their scenes, in order, after the radio fell quiet (trench1:
23 lines after openQ4's dropship direction played through; trench2: 6), with
Markswell's repeated order skipped because the player had gone inside, and
every line silent while it has no audio.
