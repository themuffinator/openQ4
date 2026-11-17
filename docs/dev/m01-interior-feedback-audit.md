# M01 dropship interior: audiovisual audit

Investigated 4 November 2026. This is evidence and an acceptance reference for
**Q4-04 in [the active roadmap](roadmap.md)**, which owns the implementation
order. The baseline below is historical; the revision-3 implementation and its
qualification are recorded at the end of this document.

## Scope and baseline

Review the complete interior sequence: briefing, incoming fire, hull breach,
failed rescue, evacuation preparation, Tak cutting the harness, descent and
gameplay handoff. Preserve the dialogue, story, character identities and the
recent repair-shot face lighting. The aim is a convincing progression from
routine flight to a failing aircraft, with quieter moments that serve the story.

The full original hangar-to-gameplay path ran in a fresh, hidden, windowed
OpenGL session with input disabled. Engine `screenshot` captures, active OpenAL
voice listings, beat logs and the OpenAL Soft wave writer provided the evidence.
The interior lasted 132.912 game seconds and reached gameplay at 283.008.
This run did not exercise save/load, skipping, Vulkan or physical audio devices.
PCM measurements and active voices establish output, not subjective audibility;
headphone and speaker listening remains an acceptance requirement.

The tested staged package contains the Tak lighting change from `42200dc6f`.
Binary/package SHA-256 identities are in `playback/result.json` below. Concurrent
save-compatibility work is moving that scene into an immutable `m01_dropship-v2`
script and retaining the original filename for older saves. Implement against
the resulting revision scheme; do not edit a historical script in place or
assume this captured package represents subsequent working-tree changes.

Local evidence, beneath `E:/Repositories/openQ4/.tmp/m01-interior-audit/`:

- `playback/result.json`, `playback/profile/q4xbase/logs/m01.log` and
  `playback/profile/q4xbase/screenshots/`: full production-path run.
- `playback/m01-mix.wav`: stereo 48 kHz, 16-bit output recording.
- `contact.png`, `voice-presence.json`, `audio-metrics.json`: visual overview and
  derived measurements. Capture names are scheduled command offsets, not game
  timestamps. Beat-to-PCM offsets use polled file sizes and are approximate.
- `loop-probe/`: bounded, direct-cabin diagnostic with `s_showStartSound 1`.
  Its expected lack of gameplay handoff is not a production failure.

Reproduce the full run with the existing harness and a fresh output directory:

```powershell
python tools/tests/awakening_m01_cinematic.py --runtime .install --retail "E:/SteamLibrary/steamapps/common/Quake 4" --awakening E:/Games/Quake_4_Alpha-main --output .tmp/m01-interior-audit/playback --cache .tmp/awakening-m01-m02/cache --duration 310 --capture-start 140 --interval 5 --audio
```

## Findings

### Sound exists, but the scene has little evolving environmental detail

The eight helper cues in
the sound declarations (`content/baseoq4/pak0/sound/openq4_m01_cinematic.sndshd`)
use deliberately restrained gains: cabin -18 dB, wind -22, impact -14, breach
-10, engine failure -17, alarm -25, harness -17 and welding -24. The cabin loses
another 5 dB after the breach. All are global and bypass occlusion, including
the small local actions, so the harness and welder provide no positional cue.

In the full run, the cabin loop remains present across briefing, breach and
repair. Wind joins after the breach. Welding is present at gain 0.062, using
the resolved `.wav` sample; checking only the shader's `.ogg` spelling would
incorrectly report it absent. Several later snapshots contain only cabin and
wind, with neither speech nor another environmental layer. The alarm is a
one-shot. The non-looping `m1_action` music starts during the earlier hangar
sequence and is absent from snapshots after the early cabin briefing, until
gameplay music starts. The exact score end/stop needs tracing before deciding
whether to extend, replace or intentionally leave it out.

The separate direct-cabin probe reproduces an explicit helper-emitter stop
23.069 seconds after the cabin loop starts. The full production-path run does
not reproduce that early loss. Background hangar events still appear in the
focused probe, making test-path interference a hypothesis, not a proven cause.
Repair the diagnostic isolation and trace the stop's caller before treating it
as a production sound defect.

Broad mix RMS falls from approximately -14.8 dBFS during the briefing to
-17.5 during the first repair interval and -28.6 between repair completion and
landing. These windows include dialogue and fades; they are not measurements of
individual cue loudness. The complete recorded file has 26 sample values at a
16-bit rail, which warrants a headroom check but does not establish audible
distortion. Rebalance cue groups and inspect stems before raising overall gain.

### The camera path does not carry the intended impacts

The sound declarations explicitly omit `shakeData`, relying on actor reactions.
The seated characters rock, but a shot's viewpoint does not consequently move.
Ordinary player views use `idPlayerView::ShakeOffsets`; cinematic views instead
take `GetViewParms` from the active camera. The render-time refresh can obtain
those camera parameters again. Adding sound shake data alone therefore does not
establish that a cinematic shot will respond.

Relevant implementation points are `src/game/Camera.cpp`,
`src/game/Player.cpp`,
`src/game/PlayerView.cpp` and the
sound emitter shake evaluation in `src/sound/snd_emitter.cpp`.
Any added response needs one owner at the final camera-sampling stage, composed
with authored movement rather than written back into the camera entity. It must
not be overwritten by a later refresh or applied twice to the view/listener.

### Lighting has an emergency transition, but few later dramatic changes

There is a real normal-to-red transition, a short fade of normal cabin lights,
an existing flicker material on an evacuation emergency light, and a cold fill
introduced at the breach. The issue is not an absence of all lighting changes.
Subsequent engine failures and impacts lack coordinated changes in fixture
output and local illumination, leaving much of the crisis on a similar plateau.

Tak's close-up is especially sensitive to camera angle and the emergency key
behind him. Preserve the recent warm-neutral seat-side fill, his original head
and normal maps, and the earlier thumbs-up shot. A welding light should add
short-lived motivation and detail without becoming the only readable face light.

### Effects are brief and disconnected from sustained actions

The current scene helper calls its spark effect three times: at the breach and
once in each welding interval. The referenced installed effect emits sparks for
about 0.1 seconds and light for 0.12 seconds; particle lifetimes are roughly
0.5–1.5 seconds. Welding audio runs for approximately 2.95 and 10.7 seconds.
Two brief world-position bursts cannot describe those sustained tool contacts.

The added helper supplies no persistent dust, smoke, loose debris or damaged
fixture sequence. This does not mean the original map contains no effects.
`ui_reducedFlashing` currently skips the entire helper spark call, including its
embedded sound. A future reduced-flashing version should retain non-flashing
audio and readable physical consequences.

The installed original map script's scene comments describe stronger AA fire,
heavy tremors, alarms, flickering lights, changing engine pitch and a hull
trembling apart. They support the intended escalation, but comments alone do
not demonstrate a functioning effect.

## Proposed direction by beat

Times below are seconds from cabin entry in the recorded full run, rounded for
orientation. Implementation should follow semantic/animation events, not these
absolute offsets or the screenshot filenames.

| Beat | Present weakness | Proposed response |
| --- | --- | --- |
| Briefing, 0–29.5 | Visually stable cabin; thin environmental identity | Continuous engine/airframe bed with light cabin rattles and restraint foley. Keep dialogue forward and lighting steady enough to establish normality. Any flight motion remains subtle. |
| Incoming fire, 29.5–42.7 | Actor reactions carry most of the impact | Pair each authored hit with a directional thump, short damped camera jolt, cabin rattle and one localized fixture dip. Escalate the second hit; establish the alarm and a strained engine layer. |
| Breach, 42.7 | Model swap and short spark have limited shared impact | Align tearing/explosion, strongest bounded camera impulse, brief power loss/recovery, breach light and a small dust/debris burst. Sustain wind and damaged-engine texture after the event. |
| Failed rescue, 46.4–67.9 | Background can feel empty after the loss | Retain restrained wind, metal strain and damaged-light behavior. Briefly reduce the impact layers around the deaths and command response, then rebuild urgency. Keep faces and the loss readable. |
| Evacuation and thumbs-up, 67.9–91.2 | New cabin set can feel reset or disconnected | Carry the damaged sound/light state across the set change. Add spatial harness releases, boots, rail and zipline handling at visible contact events. Preserve Tak's successful thumbs-up composition. |
| Harness cutting, 91.2–117.2 | Long tool sound with momentary sparks; weak physical contact | Bind bounded spark emission, tool noise and local weld glow to the actual hand/tool contact intervals. Maintain the face fill. Use intermittent airframe strain and a distinct late engine failure to sustain urgency between lines. |
| Release, descent and handoff, 117.2–132.9 | Environmental fade becomes very sparse before landing | Make the latch release distinct, blend wind/rope movement toward exterior ambience, and give landing a restrained contact sound and camera settle. Clear scene-owned effects at skip/handoff without cutting the world ambience abruptly. |

## Implementation constraints and acceptance

Use a small, scene-owned cue interface that dispatches the related sound,
camera response, light envelope and effect from the same beat. Define light hit,
heavy hit and breach response tiers; give each a measurable duration and maximum
amplitude after a prototype review. Avoid continuous high-amplitude shaking,
repetitive white flashes and arbitrary effects unrelated to a visible cause.
Short impacts should settle promptly; long damage beds should evolve without
masking dialogue. Resolve framing/occlusion before adding visual density.

Carry semantic damage state across the two interior sets, but use each set's
own attachment points. Use local emitters for near-field actions, broad cabin
beds for airframe/engine sound, and explicit channel ownership so concurrent
harness actions do not cut one another off. Keep impact transients and their
tails independently controllable where needed. Review music continuity and
ducking deliberately rather than restarting a score at every cut.

Preview suitable assets already supplied by the player's installation:
`dropship_evasive1/2`, `dropship_gaineng/loseeng`, `space_battle_ambient*`,
`space_explo_ship`, hull creaks/rattles, existing weld effects and small dust or
smoke effects. Verify shader parsing, dependencies, scale, orientation and
performance before reuse. Reference installed assets; do not copy proprietary
media into the repository or generate a replacement face for this pass.

The existing `ui_retainedReducedMotion` controls decorative UI animation, not
cinematic camera movement. Provide an explicit cinematic shake amount/off
control using the project's localized settings conventions; do not silently
reinterpret that UI setting. Reduced flashing must suppress sharp light pulses
while preserving audio, stable face light and non-flashing damage cues. Respect
VR comfort behavior rather than forcing extra head motion.

Acceptance for Q4-04:

- Record the full production entry on OpenGL and Vulkan with matching staged
  binaries/content. Use engine screenshots around each beat at finer intervals
  than this audit's five-second survey; use continuous camera/envelope telemetry
  for motion. Sparse stills cannot establish shake quality or particle timing.
- Trace start, stop, gain and owning entity for each cue. Verify environmental
  beds through their intended phases, all welding contact intervals, score
  continuity and intentional quiet periods. Check headroom with dialogue and
  effects together, then audition headphones and speakers, including supported
  stereo/HRTF configurations. Do not accept the mix on RMS or voice count alone.
- Demonstrate a distinct progression in all response tiers and synchronized
  cue dispatch within one game tick. Check shot boundaries, camera penetration,
  Tak's face/eyes, tool placement and sightlines during the squad's departure.
  Establish particle, light and voice budgets against the captured baseline.
- Compare normal, reduced-flashing and shake-off modes; retained dialogue,
  readable faces and story information must survive every setting. Qualify
  pause/resume and high-refresh timing without accumulating camera offsets.
- Under the immutable scene-script/save policy, check fresh entry, applicable
  old saves and saves in each supported scene phase. Exercise skip in every
  phase and replay/restart; require no surviving scene camera offset, looping
  cue, light, emitter or callback after cleanup. Verify ordinary on-foot play
  and retail/MP behavior remain unaffected.
- Update player-facing candidate release notes only when the changes exist and
  their tested scope is known. This planning audit does not promote the feature
  or close Q4-04.

## Implemented revision 3, 4 November 2026

Fresh M01 starts use `m01_dropship-v3.script`. Original and lighting-only v2
scripts remain available under their immutable names for suspended saves.
The complete dialogue token sequence and the repair face-fill block are
unchanged from v2. Interior duration remains **132.912 game seconds**, with
cabin entry at 150.096 and gameplay handoff at 283.008 on both renderers.
Restart M01 to receive the new direction; loading an earlier save deliberately
retains that save's scene revision.
Saves from the interim unversioned lighting development build may require a
map restart; retaining v1 and v2 does not make two different programs under the
same old filename interchangeable.

### Delivered behavior

- Cabin, wind and airframe rattle persist across the interior sets. Environmental
  beds duck 4 dB during overlapping speech, with an additional 3 dB reduction
  during the rescue quiet beat and welding. The local tool ducks 2 dB during
  speech. Per-actor foley supplies harness, gear, boot and rappel contacts.
- Light/heavy/breach cues dispatch sound, camera envelope, fixture dip and
  damage effects in the same script tick. Their strength/duration parameters
  are 0.9 degrees/0.70 s, 1.8/1.05 and 2.8/1.40 respectively. Damped rotation
  composes once after the authored camera sample, with no camera translation,
  accumulated state, new save bytes or effect on ordinary player views.
- A modest rattle/damage loop and small smoke leaks sustain the damaged cabin.
  Cold breach/exit light, short fixture dips and a distinct late engine failure
  carry the progression into evacuation. The repair face fill remains steady.
- Both cutting intervals own a positional tool emitter, low-amplitude blue glow,
  small sparks and smoke following Tak's right index-finger joint. Sound ranges
  use the engine's actual distance units. Effect calls use declared `fx_*`
  keys, as required by the entity script API. The visible smoke/glow is restrained;
  sparks are small and the player-harness contact is mostly below the close-up
  crop. A culling-disabled diagnostic did not materially change that framing.
  Effects were not moved across Tak's face to exaggerate their visibility.
- The descent adds rope movement and a restrained landing contact/settle.
  Scene emitters, lights, loops and worker threads are removed at handoff and
  after the engine's normal skip path.
- **Cinematic shake** is localized in all twelve language tables and exposed
  in retained and classic game settings. Zero disables the added motion;
  `vr_enable` also forces it to zero. Reduced flashing omits spark bursts and
  fixture dips, steadies the evacuation emergency material and weld glow,
  and retains sound, smoke and face light.

The original non-looping `m1_action` score lasts 51.691 seconds. Its traced
start and natural expiry explain its disappearance early in the cabin; it is
not restarted at the breach or during the deaths. New environmental layers
supply continuity after it ends. The focused diagnostic now cancels the hangar
script threads, but still contains map ambience, so only full production entry
recordings qualify the mix.

### Qualification evidence

Evidence root: `E:/Repositories/openQ4/.tmp/m01-feedback/`. All visual evidence
comes from engine `screenshot`; tests used hidden windowed sessions without
mouse/keyboard input. Each result records binary and content SHA-256 identities.

| Check | Result and evidence |
| --- | --- |
| Full hangar, cabin, evacuation, repair, descent and gameplay | `final-gl/` and `final-vk/`: PASS, no scene runtime errors, identical semantic beat times, full cleanup and ordinary gameplay after handoff. One-second survey plus breach/tool/engine-failure capture bursts. |
| Stereo and HRTF output | Both full runs recorded OpenAL PCM; Vulkan explicitly enabled Built-In HRTF. The user auditioned the repair excerpt and confirmed **dialogue is clear**. This does not claim a separate audition on both physical speaker and headphone systems. |
| Save/load in eight phases | `phase-checkpoints/`: actual named autosaves and restores at cabin, incoming, breach, rescue, evacuation, first weld, engine failure and descent. PASS; all eight fixtures retained. |
| Skip in eight phases | Fresh process restore and real engine skip for each fixture; `skip-matrix.json`: 8/8 PASS, gameplay and all helper/light counts zero. |
| Older scene revisions | `legacy-v1/`, `legacy-v2/`, `legacy-matrix.json`: both existing old-revision fixtures restore and resave/reload successfully. These are compatibility fixtures, not a claim of every historical mid-scene save. |
| High-refresh configuration and repair restore | `repair-restored/`: fresh repair restore, 144 FPS cap, normal time, rendering, audio and full handoff PASS. Several concurrent test games prevent a claim of sustained 144 FPS performance. |
| Reduced flashing and shake off | `reduced-off/`: full restored cabin through gameplay PASS; every recorded added camera angle is zero, sound and dialogue remain, helper cleanup complete. |
| VR comfort setting | `vr-comfort/`: restored late repair through gameplay PASS, all added camera angles zero with `vr_enable 1`. No physical headset qualification is claimed. |
| Pause/time hold | At repair, simulation time remains 92944 during the diagnostic stop/resume. Production retained-pause clock/cache/resume tests also pass; this is not a manual pause-menu interaction test. |
| Settings | `settings-final/`: engine screenshot shows the localized control without overlap; semantic left action changes 1.0 to 0.9 and the CVar reads back 0.9. Developer-off `testAutoSave` is refused. Generated-screen consistency, GUI clipping and retained adapter tests pass. |
| Current integrated build | After staging, `current-final/` repeats the complete production path on GL, and `current-vk/` restores late repair through gameplay on Vulkan: both PASS. The independently copied matching runtime avoids concurrent restaging of `.install/`; `current-runtime.json` records its identity. The interrupted shared-install attempts are not counted as passes. |

`final-metrics.json` records sampled maximum camera rotations: GL
1.764/0.738/1.509 degrees (pitch/yaw/roll), Vulkan 2.127/0.756/1.800.
Sampling and differing rendered frames explain the difference; these are
observations, not formal maxima. Flight motion stays below 0.3 degrees per axis.
There is no translation into nearby geometry. Tak's earlier thumbs-up and
repair face/eyes remain readable in both full captures.

The added persistent scene budget is at most three shadowless lights including
the existing face fill, three helper entities including the tool source, and
two worker threads. The cue owner has eight explicitly owned sound channels;
local actor foley owns one channel per actor. The tool creates at most one
spark burst every 0.35 s and one small smoke effect every 1.8 s, only during
its two contact intervals; damage smoke is spaced 5.7 s apart. All are bounded,
non-looping installed particle definitions. Whole-map maxima were 80/84 BSE
effects and 2187/2152 particle units (GL/Vulkan); these include unrelated map
effects and are **not** an isolated overhead measurement against the baseline.

PCM contains 14/3 exact rail-valued samples in the complete GL/Vulkan recordings,
with 4/1 inside the approximate interior windows and 4/0 during repair.
This is not a zero-clipping certification or evidence of audible distortion.
The repair was audibly accepted by the user. Tool gain is 0.523 during speech
and 0.707 between lines, while its source is much quieter than the engine beds;
raw emitter gain alone cannot compare their perceived loudness. Bed gains during
repair speech are approximately 0.088/0.070/0.031, and speech remains unattenuated.

Reproduce a full normal-time run using the matched runtime:

```powershell
python tools/tests/awakening_m01_cinematic.py --runtime .tmp/runtime/candidate --retail "E:/SteamLibrary/steamapps/common/Quake 4" --awakening E:/Games/Quake_4_Alpha-main --output .tmp/m01-feedback/replay --cache .tmp/awakening-m01-m02/cache --duration 315 --capture-start 140 --interval 1 --audio --trace --finish-after-handoff
```

Use `--api vk --hrtf` for the other full configuration. Use `--checkpoint-all`
for phase fixtures and `--load-save <fixture> --skip-beat <phase>` for skips.
Accelerated/no-render lifecycle results do not qualify audiovisual timing.

The pass is implemented and scene-qualified within these limits. Q4-04 remains
open for the broader thirteen-map campaign gate. Physical-device comparisons,
headset comfort and an isolated performance baseline remain broader qualification
work; no claim of those results is made here.

## Revision 4, 17 November 2026

User review of the scene raised five defects. Fresh starts now compile
`m01_dropship-v4.script`; v1-v3 stay in pak0 for their saves. v4 also redirects
the hangar relay (`intro_unit1_seq_2_trigger_seq_3` -> `oq4_m01::hangar`), and
`M01Cinematic.inl` keeps every original callback if any of the three anchors
differs. `tools/tests/awakening_m01_dropship_contract.py` pins what follows.

| Defect | Cause | Change |
| --- | --- | --- |
| Marines snap 90 degrees onto the ramp | AAS route south along x 6836, east along y 7690, north up the ramp; directional run animations change travel direction within one or two frames at each corner (traced at 10 Hz). | `boardLane` moves a `target_null` 56 units ahead along a lane with 96 and 64 unit corner radii that enters the ramp straight; one continuous `scriptedMove` follows it. The seven lane marines get `walkRange 24` and `noSimpleThink 1` (a simple think re-paths only when the AI manager's round robin reaches it, so the AI stopped at the carrot's first spot). `rvAIManager::IsSimpleThink` honours that key. Markswell's authored stop-and-turn route is unchanged. |
| Seated marines shake before the hit | `st_dropship_chair` is a turbulence loop: the body bounces about 0.4 units every half second and the neck jolts every quarter second. | Until the first impact the loop runs at a fifth of its rate (`calm`), and the camera's flight sway is 0. Full turbulence starts with the tier-1 hit. |
| Tak frees harnesses that are not there | The damaged cabin mesh (`env_q4x_trench_dropship`) kept harness geometry only for the two seats that tear away. The expansion's separate `env_q4x_dropship_harness` (one harness, `open`/`mitchell_open`/`dekard_open`) was never placed. | Six harnesses fill the surviving seats of both damaged sets (breach and evacuation), placed from the ship's origin and yaw; the ship's harness joints never move in any of its clips. Each `open` starts with its marine's `st_chair_to_idle` (hands push up 0.88-1.33 s, harness swings 0.75-1.33 s), Tak's with `st_tak_zipline_start`, Mitchell's once Tak frees him, Dekard's on "Got it! Dekard, let's go!". |
| Seat names belong to Quake 4's squad | Both cabin meshes use retail's `nameplate_<name>` materials. | Two skins (the damaged mesh swaps two plates' places) map them to openQ4's own plates for Tak, Darnell, Navajas, Dekard, Townsend, Atkins, Mitchell and Markswell, drawn by `tools/assets/awakening_nameplates.py` in openQ4 Chain. Only the v4 scene applies them. |
| Marines rappel beside the rope | Fixed stride counts left every marine 15-60 units from the line (Tak about 60). | The line's `joint20` (where the hands pass the cabin floor) and the grip offset measured in `st_repelling` (20.5 ahead, 2.6 left) give one mark. Each marine faces it, walks whole strides with the remainder absorbed in the last one, faces out of the breach and settles onto the mark before `st_repelling`. |

Evidence (`.tmp/m01-polish/`, hidden windowed OpenGL, engine screenshots, no
input): `baseline/` is v3 on the same binaries; `final-gl/` is v4 from the
hangar through gameplay handoff with no script errors. All five rappels log
their start on the mark (`M01_REPEL`). Plates read in the rescue and
evacuation shots; harnesses sit on the seated marines and stand open on vacated
seats. `lane1/` traces the lane marines: each runs the whole lane without a
stop, and the ramp turn spreads over about 0.7 s (peak 18-27 degrees per tenth
of a second) where AAS turned it in one or two frames. The longer walks to the
rope move gameplay handoff from 283.0 to 285.2 s.

Follow-up checks on the committed v4 (`fc5bb1138`), same binaries:

| Check | Result and evidence |
| --- | --- |
| Vulkan, whole scene | `final-vk/`: PASS; beat times identical to OpenGL, all five rappels on the mark, cleanup complete. Harnesses, plates and the hangar run match the OpenGL stills. |
| Save/load in eight phases | `checkpoints/`: in-process autosave and reload at cabin, incoming, breach, rescue, evacuation, first weld, engine failure and descent; all eight fixtures retained, the scene reaches handoff. PASS. |
| Save/load mid-hangar | `checkpoint-hangar/`: saved while the lane marines run; after the reload the scene keeps the same beat times (cabin 150.0, handoff 285.2) and every rappel lands on the mark. PASS. |
| Skip in nine phases | `skip/<phase>/`, `skip-matrix.json`: each fixture loaded in a fresh process, then the engine's cinematic skip; 9/9 reach gameplay with helper and light counts zero. PASS. |

Not covered: a viewer's judgement of the slowed seated loop and of the ramp
curve; the scene's motion was judged from traces and one-second stills, not
video.

## Revision 5, 17 November 2026

The landing zone's marines (`darnell`, `tak` and four Atlas marines, all
`hide 2`) were only shown at touchdown, so the escape camera descended onto
empty ground and the squad popped in as it landed. `m01_dropship-v5.script`
shows Darnell and the Atlas fire team once Darnell's rappel ends and Tak once
his does; `landing()` triggers only those not yet shown. The Atlas marines get
`cinematic 1` at spawn (they lacked it, and non-cinematic entities do not think
during a cinematic). Evidence: `.tmp/m01-polish/descent/` (OpenGL, whole scene,
handoff at 285.2 s as in v4); a marine is in view during the descent where v4
showed none. v4 stays in pak0 for its saves.

## Revision 6, 17 November 2026

User review: the lieutenant standing at the front of the cabin (Markswell)
dropped into a rifle stance before he spoke, and sat down in the aisle after
the first hit. `st_markswell_enters`, `st_markswell_talks` and
`st_markswell_sits` are consecutive ranges (1-101, 102-480, 481-644) of one
performance; the cabin's `markswell_sits` (frames 481-575) lowers his seat.

| Defect | Cause | Change |
| --- | --- | --- |
| Rifle stance before the briefing | The entrance started with Vansen's acknowledgement (5.4 s with the radio) but reports done after 3.9 s, and `endWithIdle` returned him to his armed idle for about a second before `st_markswell_talks`. | The entrance starts 1.3 s into the acknowledgement and the briefing as it ends. All three clips end without idle, so any remaining gap holds the entrance's last frame, which is the briefing's first. |
| Sits down in the aisle | The sit's root motion carries him 45.8 units across into his seat, but his movement box met the hidden seated stand-in (`markswell_sitting`, 348 2918) after 5.6 units and, with that removed, the seat's collision after 14.7. He finished the clip mid-aisle; the stand-in later appeared 33 units away. | `disableClip()` for the sit (the clip has no vertical travel, so losing gravity is harmless). He ends at 347.96 2925.7, the clip's authored seat; the stand-in at 2918 replaces him on the cut to the wide shot. |

Evidence (`.tmp/m01-markswell/`, hidden windowed OpenGL, engine screenshots,
no input): `base1/` is v5 with a position probe; `fix1/` the candidate with the
same probe; `v6-focus/` the committed v6 with a save and reload as the ship is
first hit; `v6-full/` v6 from the hangar to gameplay. Not covered: Vulkan, and
judgement from video rather than one-second stills and 50 ms bursts.

## Other findings and limits

The full-run harness reported no fatal errors and a successful gameplay handoff,
but the asset log is not warning-free. Existing warnings include malformed
`m03_airassault.sndshd` declarations, duplicate declarations, missing textures,
missing combat VO and map entities in solids. In particular, verify any proposed
M03 sound reuse through a valid declaration rather than assuming that file parses.
Those broader asset/progression issues are outside this feedback plan.

The clean detached worktree `E:/Repositories/openQ4-worktrees/e931/openQ4` at
`85c61fe16` is already merged into the main checkout and is a possible cleanup
candidate. Confirm its owner is finished before removing it; no worktree was
removed by this audit.
