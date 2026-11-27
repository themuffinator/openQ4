# Half-Life: Alyx design study for openQ4 VR

Research note, 27 November 2026. This note studies how Valve's Half-Life: Alyx
(2020) shaped its VR experience. For each of its ideas, it weighs whether
openQ4's VR mode could port or adapt it. It schedules nothing. The
[roadmap](roadmap.md) stays the single plan, and the
[candidate roadmap items](#candidate-roadmap-items) below wait for the
project owner's decision.

Ideas only: openQ4 takes no code or assets from Alyx. Any change must keep to
the project's constraints:

- stock Quake 4 assets only, with fixes in engine or game code;
- game code under the Quake 4 SDK EULA;
- one unified game module;
- unchanged multiplayer wire and save formats;
- both campaigns, Quake 4 and The Awakening (`q4xbase`).

## Summary

Alyx earned its reputation through hundreds of small decisions, each made
after playtesting. No single feature carries it. Three principles recur:

1. **Keep the player's attention in the world.** Alyx has no flat HUD: health
   sits on the back of the glove and ammunition on the gun. Menus are panels
   the player points at.
2. **Spend effort where players actually look and touch.** Valve made
   everything in arm's reach respond. It cut what VR could not make feel right,
   such as melee without force feedback, two-handed guns without a physical link
   between the hands, and fast enemies.
3. **Comfort and access are a menu of choices, not one default.** Alyx offers
   three locomotion modes, both kinds of turning, a choice of hand, seated and
   standing play and a single-controller mode, and its AI treats every movement
   mode alike.

openQ4 already covers much of the second and third principles, through a
different route suited to a fast shooter built for a mouse. It has:

- continuous movement and room scale;
- snap and smooth turning, and the comfort vignette;
- a held, two-handed gun that sits at the hand;
- a full body;
- seated and standing play spaces;
- world-GUI pointing, and haptics for shots, hits and beams.

The in-progress hand menu and wrist HUD on `claude/vr-hand` move openQ4
towards the first principle.

The best returns from Alyx are the small, cheap fixes of fairness, comfort and
access that its patches and playtests turned up. Its systems built on content,
such as magazines, backpacks, puzzles and resin, do not carry over without new
assets.

**Top recommendations** (details in the [shortlist](#ranked-shortlist)):

1. Close the remaining ways a gun pushed through a wall could fire out of the
   far side (single player). See [corrections](#corrections-and-follow-up).
2. Let the movement hand be chosen apart from the weapon hand.
3. Use the stock guns' own ammunition displays and keep the wrist for the rest.
4. Add directional and event haptics: the side a hit comes from, reload steps
   and world-screen hover. Always pair them with sound or a visual.
5. Check that each headset's audio device reports itself as headphones, so
   HRTF audio turns on by itself, and force it on in VR only where one does
   not. See [corrections](#corrections-and-follow-up).
6. Add adaptive VR resolution, after Valve's adaptive-quality method, together
   with the OpenXR visibility mask.
7. Add a single-controller mode.
8. Press world screens with a finger, as well as pointing at them.
9. Show one-time contextual VR hints in all twelve languages.
10. Optionally, a Shift-style quick dash for single player.

## Corrections and follow-up

Added 27 November 2026, after the owner had four of the recommendations
built. The rest of this note keeps its first reading, with these two
corrections applied in place.

**Two items were overstated.**

- **Wall clearance (recommendation 1).** The study said a muzzle pushed through
  a wall fires from the far side. The basic case was already fixed on
  17 November (`d54c2323c`): single-player shots were traced from the eye
  through the hand to the muzzle. Three gaps remained, and `c3b70b785` closes
  them:
  - The trace used only what stops bullets. Rockets, grenades, napalm and the
    dark matter gun also stop at large-shot clip brushes, so they could still
    leave through one.
  - The trace started from the tracking origin. While crouched, that origin is
    lifted to standing eye height and can sit inside a low ceiling.
  - The laser dot, the tracer origin and the lightning beam did not follow the
    shot's own path.
- **HRTF (recommendation 5).** The study proposed turning HRTF on in VR. Auto
  (`s_openALHRTF 0`, the default) already leaves the choice to OpenAL Soft, and
  OpenAL Soft turns HRTF on when the system reports headphones. The open
  question is only whether each headset's audio device reports itself that way.
  That is a check for the headset checklist, with HRTF forced on in VR only
  where a headset does not report headphones. Its value is smaller than the
  quick-win ranking implied.

**Built on 27 November** (on `main`, not pushed; none of it is checked on a
real headset yet):

- the hand menu on the off hand, `fce31edc3`;
- wall clearance, `c3b70b785`;
- directional and event haptics, `9c2610e55`;
- wrist displays in place of the head-locked HUD by default, `ad012cbd6`.
  The owner chose to show ammunition on the weapon hand's wrist for every gun,
  including those with their own on-gun display, and to keep the HUD as an
  option (`vr_hudMode 0`);
- fingertip presses on world screens and the hand panel, `5333c58a5`.

## Sources and how far to trust them

Valve gave no GDC talk on Alyx itself. Searches turned up no talk and no
Noclip-style documentary. The richest primary source is the in-game developer
commentary (12 November 2020, more than 3 hours over 147 nodes; see
[Road to VR on the commentary][rtvr-dc]). This study reads that commentary
through the Combine OverWiki's transcript ([OverWiki commentary][ow-dc]), a fan
wiki, so attributions to named Valve speakers are second-hand until checked in
the game. The other sources are interviews, patch notes and reviews. Where a
page could not be fetched and only a search summary was read, the source list
marks it as such. Everything here is paraphrased.

## What Alyx did, by theme

### Locomotion

- **Three movement modes.**
  - Blink: an instant teleport behind a fade.
  - Shift: the same aiming, but the view rushes quickly along the path.
  - Continuous: the stick, aimed by the head or the hand.

  Sources: [Game Developer][gd-loco], [UploadVR review][uvr-rev].
- **Turning was patched in.** Launch had only a quick (snap) turn. Update 1.1
  added smooth turning with a speed setting, and an option to turn stick
  turning off ([Road to VR on 1.1][rtvr-11]). Update 1.2 split the
  movement-and-turn hand from the weapon hand, which fixed left-handed play
  ([Road to VR on 1.2][rtvr-12]).
- **The teleport cursor promises a valid place for the body.** Holographic feet
  show where you land and which way you face. Footprints trace the path
  ([OverWiki commentary][ow-dc], [Game Developer][gd-loco]). Greg Coomer framed
  the goal as a destination the body fits, reached by a path that is viable
  ([UploadVR locomotion deep dive][uvr-loco]). Tall testers kept hitting low
  pipes, so the check uses the player's real height ([PCGamesN][pcgn-tall]).
- **Teleport is treated as movement.** Footstep and clothing sounds play,
  scaled by distance and drop ([UploadVR on teleporting][uvr-tele]). A teleport
  leaves invisible proxies along its path. Soldiers react to those as if they
  had watched, so combat plays the same in every mode ([OverWiki
  commentary][ow-dc]).
- **No free jump.** Crates are mantled, and short gaps are crossed by a
  teleport-style hop ([UploadVR AMA][uvr-ama]). Ladders climb hand over hand,
  or by a short timed move when aimed at with teleport ([OverWiki
  commentary][ow-dc]).
- **Forced motion is avoided or framed.**
  - Automatic player movement was cut ([UploadVR AMA][uvr-ama]).
  - Lifts move slowly and keep the cab in view.
  - A moving chasm platform that made testers sick was replaced by fixed
    platforms.
  - The barnacle lift narrows the view.

  Sources: [OverWiki commentary][ow-dc].
- **Few comfort settings.** Road to VR's comfort checklist found no height
  slider and no player-facing vignette option ([Road to VR comfort
  checklist][rtvr-comfort]).
- **Reception.** Reviewers rated comfort excellent. Complaints:
  - teleport hops across gaps were sometimes confusing, and missed ladder grabs
    teleported players ([Road to VR review][rtvr-rev]);
  - smooth movement slid over bumps and pulled a reviewer out of cover, so he
    changed to Shift ([UploadVR review][uvr-rev]);
  - stop-start movement in large fights, and the head clipping into walls
    ([Kotaku review][kotaku-rev]).

### Hand interaction

- **Gravity gloves in three stages.** The target glows, a press locks it
  against further hand motion, and a flick pulls it. It flies on an arc that
  corrects towards the hand, with a generous catch and a haptic pulse
  ([Road to VR, Inside XR Design][rtvr-xr1], [OverWiki commentary][ow-dc]).
  They pull only what one hand could carry ([OverWiki, Gravity
  Gloves][ow-gg]).
- **Why the gloves exist.** They spare players bending to the real floor, which
  not every headset tracks well. They also cut teleporting, because a room can
  be looted from one spot ([Gamereactor interview, archived][gr-walker]). A
  version toggled by a button was used far less, so the gloves are always on
  ([Engadget interview][engadget]).
- **Doors and controls.** Doors open both ways, because VR gives no cue to push
  or pull ([Engadget interview][engadget]). Levers, cranks and dials are
  animations dragged within an authored range, not full simulations
  ([OverWiki commentary][ow-dc]). Robin Walker plans effort as a budget spent
  where players look. In VR nearly everyone pokes into vents and under desks,
  so Valve spent more there ([Road to VR, Walker interview][rtvr-walker]).
- **Weight and throwing.** Inertia suggests weight, but throws ignore that sway
  so they stay accurate. Release velocity averages samples around the peak
  ([Road to VR, Inside XR Design][rtvr-xr1], [OverWiki commentary][ow-dc]).
  Valve avoided heavy simulated physics because no force reaches the hand
  ([UploadVR on Boneworks][uvr-bone]).
- **Hands do not pass through the world.** Road to VR credited this for the
  world's solidity ([Road to VR review][rtvr-rev]). Weapons touch what hands
  can touch ([Road to VR, Inside XR Design page 2][rtvr-xr2]). No source
  describes a "ghost hand" drawn when a hand is blocked; that is unverified.

### Weapon handling

- **Three guns, all one-handed.** The pistol, shotgun and SMG are each fired
  with one hand; the off hand may steady them. Two-handed guns were cut, because
  grips drifted with no physical link between the hands ([OverWiki
  commentary][ow-dc], [UploadVR AMA][uvr-ama]).
- **Physical reloads.**
  - The player ejects, reaches over the shoulder for a magazine, inserts it and
    racks.
  - A pistol reloaded before its last shot skips the rack.
  - Shotgun shells come two at a time, four reaches instead of seven.
  - The separate eject and rack steps are demanding. Testers blamed their own
    mistakes, not the game.

  Sources: [OverWiki commentary][ow-dc], [Road to VR, Inside XR Design page
  2][rtvr-xr2], [Game Informer interview][gi-arms].
- **Ammunition on the gun.** Each gun shows its count its own way: lit segments
  on the pistol, a number on the shotgun, a ring on the SMG. Each magazine lights
  its rounds ([Road to VR, Inside XR Design][rtvr-xr1]). Chambering, an empty
  magazine and a full load each have their own sound ([Road to VR, Inside XR
  Design page 2][rtvr-xr2]).
- **Switching guns.** The player holds a button on the weapon hand and points
  at a menu there ([GameSpot guide][gs-a11y], snippet). Road to VR rated this
  behind other games' body holsters ([Road to VR review][rtvr-rev]).
- **The flashlight sits on the off hand.** It lines up with the gun in a
  two-handed hold ([OverWiki commentary][ow-dc]).
- **Upgrades bought with resin at fabricators.** Some improve feedback, such as
  sights. Others cut physical chores, such as the autoloader ([OverWiki
  commentary][ow-dc], [OverWiki, Resin][ow-resin]).

### Inventory

- **The backpack and the wrist pockets.** The backpack holds only ammunition
  and serves whichever gun is in hand. Each wrist has one pocket, for a syringe
  or a grenade ([UploadVR AMA][uvr-ama], [UploadVR on crates][uvr-crates]).
  Players found one slot per wrist tight and carried crates instead; update
  1.2.1 fixed the physics of that ([UploadVR on crates][uvr-crates]).
- **Ammunition is placed by hand.** A dynamic layer removes upcoming ammunition
  based on what the player carries. The aim is that players always have some,
  but never as much as they would like ([Road to VR, Walker
  interview][rtvr-walker], [OverWiki commentary][ow-dc]).

### Diegetic interface and menus

- **No flat HUD.** Health hearts, ammunition and resin show on the back of the
  left glove ([OverWiki, Gravity Gloves][ow-gg]). The display began as a
  wristwatch, but Alyx draws no wrists, so it moved to the hand. Key items get
  small lights rather than outlines ([Road to VR, Inside XR
  Design][rtvr-xr1]).
- **Menus are floating panels.** They are built for a laser pointer and also
  answer touch ([Road to VR, Inside XR Design][rtvr-xr1]).
- **A spectator HUD for the desktop.** It shows weapons, health, resin and
  pockets, with a smoothed camera, zoom and a choice of dominant eye for scopes
  ([OverWiki commentary][ow-dc]).
- **Subtitles.**
  - They are placed in the world. If they drift too far out of view, they jump
    to a readable place.
  - Speakers are colour-coded but not named, and there are no captions for
    sound effects.

  CanIPlayThat called them the best it had seen in VR, while criticising the
  gaps ([CanIPlayThat review][cipt]). Update 1.2 added subtitles for spectators
  only ([Road to VR on 1.2][rtvr-12]), and update 1.4 brought the count to 18
  languages ([Road to VR on 1.4][rtvr-14]).

### Combat pacing and enemy design

- **Combat was slowed after playtests overwhelmed players.** Soldiers stop
  firing while they run, the first squads carry no grenades, and enemies are
  first met at a distance or behind glass and fences ([OverWiki
  commentary][ow-dc], [UploadVR interview][uvr-int]).
- **Enemies live longer.** Players aim better in VR than with a mouse, so
  health rose and fights grew longer and smaller ([UploadVR
  interview][uvr-int]). The Suppressor and soldiers who pull back stop players
  sniping from a safe distance ([OverWiki commentary][ow-dc]).
- **Fast zombies and fast headcrabs were cut.** In VR they were too much, and
  testers could not react in time ([Wikipedia][wiki], [PC Gamer on fast
  zombies][pcg-fast], headline and excerpt). Antlions were slowed, and shooting
  off their legs slows them further ([OverWiki commentary][ow-dc]).
- **Every threat is telegraphed.**
  - Revivers scream.
  - Striders get sound cues, which ended complaints about surprise damage.
  - Manhacks announce their launch.
  - The barnacle's tongue always wraps in front of the face.

  Sources: [OverWiki commentary][ow-dc].
- **Rest beats.** Fights were tiring in body and mind. Valve added gaps, used
  puzzles as breathers, and cut some combat ([OverWiki commentary][ow-dc]).
  Even so, Kotaku found the later fights long and clumsy with teleport and turn
  ([Kotaku review][kotaku-rev]).

### Physics and the interaction budget

- **About 20 to 30 simulated objects per room** ([Game Informer
  interview][gi-arms]). There are nearly 2,000 impact sounds over more than 160
  objects ([OverWiki commentary][ow-dc]).
- **The crowbar was cut after about 18 months.** It snagged on door frames
  because players forgot they held it, and its haptics were too easy to miss.
  Melee never felt right without force feedback ([Kotaku on the
  crowbar][kotaku-crow], [UploadVR on Boneworks][uvr-bone]).
- **Cheap tricks instead of simulation.** Bottle liquid is a shader (update
  1.4). Foliage reacts through a coarse voxel field ([Road to VR on
  1.4][rtvr-14], [OverWiki commentary][ow-dc]).

### Hands and the body

- **Gloved hands with no arms.** Valve tried several kinds of arm and could not
  estimate them well across real bodies. Missing arms bother onlookers more than
  players. Invisible arms are still simulated, so a drawer can close on a wrist
  ([Game Informer interview][gi-arms], [VentureBeat on the AMA][vb-ama]). PC
  Gamer ran columns on both sides of the argument ([PC Gamer,
  arms][pcg-arms], headline only).
- **Index finger tracking.** It adds optional actions, such as crushing a can.
  A relaxed grip does not drop things, which let testers play longer ([UploadVR
  on Index interactions][uvr-index]). Hands curl around what they can grab
  ([TechCrunch review][tc]).

### Seated, standing and one-handed play

- **At launch** ([Road to VR on accessibility][rtvr-a11y], [UploadVR on one
  hand][uvr-1hand]):
  - a single controller mode;
  - height adjustment as toggles: Crouch ducks a standing player, Stand raises
    a seated one;
  - light sensitivity;
  - subtitles in ten languages;
  - a toggle for the barnacle lift.
- **Single controller mode.** It moves reloading to a gesture at the shoulder
  and the flashlight to the main hand. It also makes the Strider gun usable with
  one hand ([OverWiki commentary][ow-dc]). Testing with seated players moved
  puzzle pieces within reach ([UploadVR interview][uvr-int]).
- **Seated play was weaker than standing** ([Road to VR review][rtvr-rev]).

### Audio and haptics

- **Spatial sound.** Steam Audio's HRTF spatialises sounds such as barnacles
  and gun layers. Old Half-Life sounds were roughened so they place better
  ([OverWiki commentary][ow-dc], [A Sound Effect interview][ase]).
- **Jeff, the blind enemy who hunts by sound.** His chapter was the sound team's
  hardest. Players had to track him, be warned and learn which noises draw him
  ([A Sound Effect interview][ase]).
- **Haptics.** Catches and grabs pulse the hand. Valve found haptics too
  subtle for some players to rely on, which is part of why the crowbar went
  ([OverWiki commentary][ow-dc], [Kotaku on the crowbar][kotaku-crow]).

### Puzzles

- **The multitool.**
  - It traces wires inside walls and solves holographic sphere hacks using
    both hands and head parallax.
  - It also solves junction puzzles, opens fabricators and lockers, and
    defuses mines.

  Sources: [OverWiki commentary][ow-dc], [OverWiki, Multi-tool][ow-mt].
- **Puzzles are rest beats between fights.** A rotation puzzle was dropped for
  wrist strain, and surreal vault puzzles for stalling the pace ([OverWiki
  commentary][ow-dc]). Some reviewers found the hacks repetitive later on
  ([Kotaku review][kotaku-rev]).

### Onboarding

- **A long-playtested opening.** The opening teaches identity, setting and
  controls; Valve playtested it for two years. Players spent anywhere from half a
  minute to half an hour on the first balcony ([OverWiki commentary][ow-dc]).
- **Lessons are taught in the world, with gates.**
  - The first zombie stays fenced off until the player chooses to fight, so
    reloading can be practised.
  - Static barnacles give pressure-free practice.
  - A gate makes sure players learn the shotgun's loading, because under stress
    they fall back on the pistol.

  Sources: [OverWiki commentary][ow-dc], [UploadVR interview][uvr-int].
- **No hint system documented.** No source found describes an optional hint
  system; treat one as unverified.

### Performance

- **Valve's published method (Alex Vlachos, GDC 2015 and 2016).**
  - Hold the frame rate, at 90 Hz an 11.1 ms frame.
  - Prefer a forward renderer with at least 4x MSAA.
  - Draw the lens's hidden-area mesh first, which saves about 17% of pixels.
  - Adapt quality every frame: keep the GPU between about 70% and 90% busy.
    Drop two levels when a frame passes 90% of its budget, and raise one after
    three frames under 70%.
  - The levels run from 0.65x to 1.4x resolution, with 8x MSAA at the top.
  - Keep a floor of about 0.8x where text must stay legible.
  - Radial density masking and fixed foveation save 5–15%.
  - Treat reprojection only as a safety net.

  Sources: [Vlachos 2015][vlachos15], [Vlachos 2016][vlachos16].
- **Alyx's own settings.** Alyx exposes fidelity settings (`vr_fidelity_level`,
  `vr_fidelity_level_auto`), which PCGamingWiki lists ([PCGamingWiki][pcgw],
  snippet). Some players blame the automatic level for stutter ([Steam
  discussion][steam-drs]). Whether Alyx runs exactly the 2016 algorithm is
  unverified.
- **The engine.** Source 2 renders forward ([Valve Developer Community][vdc]).
  Vulkan arrived in May 2020 ([Wikipedia][wiki]). Level logic decides when the
  flashlight casts shadows ([OverWiki commentary][ow-dc]). The minimum
  specification is a GTX 1060 or RX 580 ([Steam store][steam]).

## openQ4 VR today

These sources describe the current state:

- the [player guide](../user/vr.md);
- the archived
  [design record](archive/2026-10-18/plans/2026-10-02-openxr-vr.md), which
  credits PreyVR;
- roadmap items VR-01 to VR-09 and AND-05;
- the [headset checklist](vr-headset-qualification.md).

Against Alyx's themes:

| Theme | openQ4 now |
| --- | --- |
| Locomotion | Continuous stick movement, by head or off hand (`vr_moveDirection`); snap (45°) or smooth turning about the head; room scale with wall sliding (single player) or usercmd-driven walking (multiplayer); physical crouch; comfort vignette for stick motion, smooth turns and vehicles; no bob, kick or shake. No teleport. |
| Hands | Weapon hand on the gun's own grip; off hand follows its controller and returns for the foregrip, reloads and draws. Laser pointing at world GUIs in single player. No grabbing of world objects. |
| Weapons | Gun held at the hand, 1.75x scale so the hands match the body; laser dot at the shot's real depth; two-handed foregrip latch; weapon wheel by hand turn or stick; scope zoom by frustum; reload, fire and switch by button. |
| Inventory | Quake 4 has no carried items beyond weapons, keys and health. |
| HUD and menus | Stock HUD on a head-locked quad below the line of sight (`vr_hudHeightOffset -0.3`); menus and cinematics on a world-locked screen; VR SETUP, one-time VR question, larger text, on-screen keyboard (VR-08/VR-09). In progress on `claude/vr-hand`: menus on an off-hand panel (`vr_menuAnchor`), health, armour and ammunition on the off-hand wrist (`vr_hudMode 1`), hover ticks (`vr_menuHaptics`). |
| Combat | Stock Quake 4 encounters, unchanged. |
| Body | Full body with legs, torso and arms that reach the hands (`vr_body 2`), seen only by the player. |
| Access | Left-handed weapon hand (swaps sticks too), seated, standing and automatic play spaces, height calibration, stick dead zone. No single-controller mode. |
| Audio and haptics | Sound listener on the tracked head; OpenAL Soft HRTF on `s_openALHRTF` (0 auto, which depends on the device saying it is headphones). Haptics for shots, hits (both hands), beams, the foregrip latch and pickups. |
| Onboarding | VR SETUP at first entry; no in-game VR hints. |
| Performance | Fixed `vr_renderScale`; dynamic resolution off in VR; per-eye TAA optional; MSAA and SMAA work; shared uploads, GUIs and static shadows between eyes (VR-04). No visibility mask, no foveation, no multiview. |

## Idea-by-idea assessment

Effort is rough:

- **S** is days;
- **M** is one to three weeks;
- **L** is a month or more, including headset qualification.

Risk covers regressions to the flat game, multiplayer, saves and the
campaigns. "SP" means single player only.

### Locomotion

| Idea | Fits Quake 4? | openQ4 today | Adaptation | Effort / risk | Blockers |
| --- | --- | --- | --- | --- | --- |
| Separate movement hand from weapon hand (Alyx 1.2) | Yes | `vr_leftHanded` swaps both together | A `vr_moveHand` choice (off hand by default, or the weapon hand); turning follows the other stick. Engine input only. | S / low | None; usercmd unchanged. |
| Shift: a quick dash along the path to an aimed spot | Partly. Quake 4 maps need jumps, and stock fights are fast. Shift moves the body through space, so triggers and AI still see it, which Blink would not. | Continuous only | SP option: aim with the off hand, check the floor path (player bounds, the real eye height as Alyx's tall-tester fix did), then drive the body along it over a few tics through the player physics, behind the vignette. No crossing gaps, ladders or jump pads; jumping stays on A. Finish or cancel before a save. | L / medium | MP excluded: server-owned movement and fairness. Scripted triggers along a path must still fire, so the move must be physical, never a set-origin. |
| Blink (instant teleport) | Poor | None | Not adapted (see [rejected](#rejected-ideas)). | — | Skips triggers; MP fairness. |
| Framed forced motion (slow lifts, cab in view, vignette on rides) | Yes. Quake 4 has many lifts, trams and rides. | Vignette for stick motion and vehicle seats; view kept level in rides | Check that the vignette reads movers the player stands on (lifts, the tram floor) and falls or jump pads, not only stick velocity. Add them, under one strength cvar, if not. | S / low | None. |
| Footstep and landing audio for room-scale motion | Marginal | Room-scale walking makes no footsteps, by design | Leave as is: Quake 4 footsteps follow the stick, and room steps are real. | — | — |

### Hand interaction

| Idea | Fits Quake 4? | openQ4 today | Adaptation | Effort / risk | Blockers |
| --- | --- | --- | --- | --- | --- |
| Solid hands: weapons do not pass through walls | Yes, and it closes an exploit | The gun hides behind geometry. SP shots leave the controller, but since 17 November (`d54c2323c`) they are traced from the eye through the hand to the muzzle, so a bullet poked through a plain wall hits it. Gaps remained: large-shot clips, the crouched starting point, and the marker and effects (see [corrections](#corrections-and-follow-up)). | SP: trace from the eye (or shoulder) to the muzzle each shot. If blocked, launch from the blocked point, or hold fire with a click and a haptic. Optionally stop the drawn gun at the surface, as Alyx's hands stop. | S–M / low | None; MP already fires from the eye. |
| Press world screens with a fingertip | Yes. Quake 4 has many door panels, lift calls and terminals. | Laser pointing plus trigger | SP: when the off-hand fingertip (from the body's hand joints) crosses a world GUI surface within a few centimetres, click there, with a haptic tick. Pointing stays. | M / low | The fingertip position needs the body's hand rig; MP keeps the view-based focus. |
| Hover ticks on world screens | Yes | Menu hover ticks in progress (`vr_menuHaptics`) | Extend the same tick to world GUIs when the cursor enters a new control. | S / low | None. |
| Pull pickups by pointing (gravity gloves) | Weak. Quake 4 items are picked up by touch, and room scale walks you over them; Alyx's reason, sparing the real floor, mostly does not apply. | Touch pickup | SP option: point at an item within range with line of sight, flick, and the item flies to the hand and is collected through the normal pickup path, so its scripts and triggers fire. | M / medium | Changes SP balance (health from range); MP excluded; saves must finish the flight. |
| Grab and throw loose objects | Weak. Quake 4 moveables are scenery, not tools. | None | SP: grab small `idMoveable` props with the off hand and throw them by peak velocity. | L / medium | No puzzle or combat purpose in stock maps; physics edge cases. |
| Physical doors, drawers and levers | No. Quake 4 doors are scripted movers that open by themselves. | Auto doors; GUIs for controls | Not adapted. | — | Needs content. |
| Finger curl and grab poses | Yes, for presence | Free off hand uses the view model's static hand pose | Curl the free off hand's fingers from the grip and trigger values, using game-side joint control on the existing finger joints; relax on release. | M / low | Visual only; the left-handed mirror and the Awakening arms need checking. |

### Weapons

| Idea | Fits Quake 4? | openQ4 today | Adaptation | Effort / risk | Blockers |
| --- | --- | --- | --- | --- | --- |
| Ammunition on the gun | Yes. Seven of the twelve base weapon decls carry a `gui` ammunition display (hyperblaster, machinegun, nailgun, napalm gun, railgun, rocket launcher, shotgun); the blaster's is commented out. | The gun displays render; ammo also on the HUD (or the wrist, in progress) | With the wrist HUD, show ammunition there only for weapons without a display of their own (blaster, gauntlet, grenade launcher, lightning gun, dark matter gun, and Awakening weapons until checked). Confirm the gun displays read at 1.75x and in both eyes. | S / low | None; stock assets already carry the displays. |
| Distinct weapon-state sounds | Mostly present: Quake 4 has dry-fire and reload sounds | Stock sounds | Nothing to add without content; pair them with haptics (below). | — | New sounds would be content. |
| Gesture reload (Alyx's single-controller shoulder gesture) | Yes, as an option. The stock reload stays a canned animation. | X reloads | Optional `vr_gestureReload`: squeezing the off-hand grip over the shoulder or at the belt starts `_reload`. The button stays. | S–M / low | Input only; MP-safe. |
| Shoulder and hip quick slots | Possibly. Quake 4 carries up to 12 guns, too many to holster each. | Wheel and last-weapon button | Two body slots (over the shoulder, at the hip) bound to chosen weapons or last weapon, drawn by reaching and squeezing. | M / low | Input only; must not fight the foregrip test. |
| Weapon selection at the hand | Yes | Wheel on the HUD, by hand turn or stick | After the hand panel lands, open the wheel world-locked where the weapon hand was, as Alyx's menu appears at the hand. | M / low | Depends on the `claude/vr-hand` panel work. |
| Off-hand flashlight | Partly. Quake 4's flashlight is part of each weapon. | Y toggles the weapon light | Possible: move the light's origin to the off hand while it is free. Low priority, because the gun's lens glow would no longer match. | M / medium | Stock weapon light defs; MP shows other players' lights. |
| One-handed-only guns (Alyx cut two-handed weapons) | No | Two-handed foregrip latch | Keep the latch: Quake 4's guns are rifles in the stock models. Alyx's drift lesson argues for keeping the latch forgiving, so it holds until released rather than needing the hands to stay aligned, which openQ4 already does. | — | — |

### Interface, HUD and menus

| Idea | Fits Quake 4? | openQ4 today | Adaptation | Effort / risk | Blockers |
| --- | --- | --- | --- | --- | --- |
| Status on the off hand, nothing flat | Partly. The stock HUD also carries objectives, the damage-direction arrows and multiplayer scores. | Wrist HUD in progress (`vr_hudMode 1`) | Land it; keep head-locked whatever must be seen without looking (damage direction, captions, objective prompts, multiplayer markers). | M (in flight) / low | Stock HUD GUIs; both campaigns' HUDs. |
| Menus at the hand, pointer and touch | Yes | Off-hand panel in progress | Land it; let a fingertip press panel buttons as well as the laser (shared with world-screen touch). | S after the panel lands / low | — |
| Spectator view on the desktop | Yes, for streaming and onlookers | `vr_mirror` 0/1/2 (black, raw left eye, the screen) | A smoothed mirror: the left eye with rotation smoothing, cropped to the window, with the flat HUD drawn over it. | M / low | Extra 2D pass on the desktop only. |
| World-placed captions that jump back into view | Yes. Captions exist (AUD-07) and draw above the HUD. | Captions on the head-locked HUD layer | Keep captions in a comfortable band in front of the eyes, not on the wrist. Optionally point towards an off-screen speaker. Alyx's speaker colours are an option where captions name no speaker. | S–M / low | Strings stay in the language tables. |

### Combat pacing and enemies

| Idea | Fits Quake 4? | openQ4 today | Adaptation | Effort / risk | Blockers |
| --- | --- | --- | --- | --- | --- |
| Slower, longer-lived, better-telegraphed enemies | No, for stock play. Quake 4's encounters are fixed by its maps, scripts and skill levels. | Stock | Do not change by default. First measure whether laser aim makes VR easier or harder (accuracy and damage taken on a fixed route); only then consider an opt-in single-player VR skill scale. | — / high | Stock behaviour; MP impossible; save balance. |
| Telegraph by sound and touch | Yes | Hits buzz both hands equally | Directional hit haptics: weight the pulse to the hand on the side the damage came from. Add a pulse for nearby explosions by distance. Keep the HUD's damage arrows head-locked. | S / low | None; local feedback only. |
| Rest beats, ammo by hand | Already Quake 4's own design | Stock | Nothing to do. | — | — |

### Body and hands

| Idea | Fits Quake 4? | openQ4 today | Adaptation | Effort / risk | Blockers |
| --- | --- | --- | --- | --- | --- |
| Hands only, invisible arms | Contrasts with the owner's choice of a full body | `vr_body` 2/1/0 | Keep the body as decided. Alyx's reason was arms estimated wrongly across bodies, which is the thing to watch in headset reports (VR-02 checklist 3.9). A hands-only choice needs the forearm and hand split, which the shared stock arm mesh does not allow cleanly. | — | Shared view-arm mesh. |
| Invisible arms for physics | Marginal | None | Only useful with grabbing (above). | — | — |

### Access

| Idea | Fits Quake 4? | openQ4 today | Adaptation | Effort / risk | Blockers |
| --- | --- | --- | --- | --- | --- |
| Single-controller mode | Yes. Quake 4 needs few actions beyond move, turn, fire, jump, crouch, reload and switch. | None | `vr_oneHanded`: one stick moves, turning by head or a stick flick, grip for the wheel, face buttons for jump and crouch, gesture reload, the flashlight on a long press. The VR SETUP row and strings in all 12 languages. | M / low | Input only; MP-safe. |
| Crouch and stand toggles for seated players | Mostly present | Seated mode sets standing height at recentre; B crouches | Check that `in_toggleCrouch` applies to the VR crouch button, and say so in the guide. | S / low | — |
| Light sensitivity | Yes. Quake 4 flashes the screen on hits, pickups and explosions. | No shake in VR | One VR scale for full-view flashes and blobs, drawn in each eye's 2D pass. | S / low | None. |

### Audio and haptics

| Idea | Fits Quake 4? | openQ4 today | Adaptation | Effort / risk | Blockers |
| --- | --- | --- | --- | --- | --- |
| HRTF in VR | Yes. Headset audio is nearly always headphones. | Listener on the head; HRTF on auto, which OpenAL Soft already turns on when the system reports headphones | Check on each headset whether its audio device reports headphones (the log says whether HRTF is active when the device opens). Only where one does not, request HRTF while a headset presents and `s_openALHRTF` is 0; 1 and 2 keep their meaning. | S / low | Needs headsets to check. |
| Richer haptics, never alone | Yes | Shot, hit, beam, latch, pickup | Pulses for reload steps (from the reload animations' existing frame events), weapon switches, world-screen hover and landing from a fall. Every pulse pairs with a sound or a visual, because Valve found haptics too subtle for some players. | S–M / low | None. |

### Puzzles and onboarding

| Idea | Fits Quake 4? | openQ4 today | Adaptation | Effort / risk | Blockers |
| --- | --- | --- | --- | --- | --- |
| Multitool and hologram puzzles | No | None | Not adapted. | — | Needs content. |
| Teach in the world, at the moment of need | Yes, as hints | VR SETUP only | One-time contextual hints, localised and shown once each, then remembered in an archived bitmask cvar. Examples: the first two-handed weapon (foregrip), the first scoped weapon (off-hand zoom), the first world screen (point or press), the first vehicle seat, the wheel, recentring. On the hand panel or under the HUD. | M / low | New `#str_*` strings in all 12 languages; nothing in saves. |
| A practice room | No | None | Not adapted; would be a new map. | — | Content. |

### Performance

| Idea | Fits openQ4? | openQ4 today | Adaptation | Effort / risk | Blockers |
| --- | --- | --- | --- | --- | --- |
| Adaptive resolution (Vlachos 2016) | Yes. The HUD and menus are their own quad layers, so text stays sharp at any eye scale. | Fixed `vr_renderScale`; dynamic resolution off in VR | Render each eye into a sub-rectangle of its fixed target and submit that rectangle (`XrSwapchainSubImage::imageRect`), with no swapchain re-creation. The level is chosen from GPU time with Valve's thresholds, with an upper bound at `vr_renderScale`. Per-eye TAA history resets on a level change. | M–L / medium | Needs GPU timing on both backends; headset tuning (VR-04). |
| Hidden-area mesh | Yes | Not used | Use `XR_KHR_visibility_mask` and draw the mask at the near plane in depth before the depth pre-pass, rather than in stencil, which Quake 4's shadow volumes use. | M / low | Runtime support varies; skip when absent. |
| MSAA as the VR default | Likely | `r_temporalAA 2` leaves TAA off in VR; MSAA by preset | Decide a VR anti-aliasing default (4x MSAA where the budget allows) and measure it against the refresh budgets. | S / low | Preset policy; GPU budget. |
| Fixed foveation on Quest | Yes | None | `XR_FB_foveation` profiles on the Quest flavour's swapchains (AND-05). | S–M / low | Emulator cannot judge it; needs a headset. |
| Fixed foveation on PC, multiview stereo | Possibly | None | Vulkan fragment shading rate, then `VK_KHR_multiview` / `GL_OVR_multiview`. | L / high | Large renderer work; per-eye subviews and TAA. |

## Ranked shortlist

Ranked by value for the effort, within each group.

### Quick wins

1. **Shots cannot leave through a wall** (SP launch clearance). This is a
   fairness and correctness fix. The basic case was already fixed; the
   remaining gaps closed an open lead from the body round (done,
   `c3b70b785`).
2. **Movement hand chosen apart from the weapon hand** (`vr_moveHand`). Alyx
   needed a patch for this, and players asked for it.
3. **Ammunition on the stock guns' own displays**, with the wrist showing the
   rest. This needs no new content, and it fits the wrist HUD now in progress.
4. **Directional hit haptics and event pulses.** Every pulse pairs with a
   sound or a visual.
5. **An HRTF check per headset.** Auto already turns HRTF on for devices that
   report headphones; force it in VR only for headsets that do not.
6. **Vignette for movers, falls and jump pads**, after checking what it reads
   today.
7. **Light-sensitivity scale for full-view flashes in VR.**
8. **Crouch toggle check** for seated players, and a line about it in the
   guide.

### Medium

1. **Adaptive VR resolution and the visibility mask** (VR-04). They are the
   largest frame-time gain without a quality setting.
2. **Single-controller mode.**
3. **Fingertip presses on world screens and menu panels**, with hover ticks.
4. **One-time contextual VR hints.**
5. **Gesture reload**, with **shoulder and hip quick slots** as a follow-on.
6. **The weapon wheel at the hand**, after the hand panel lands.
7. **A smoothed spectator mirror** with the flat HUD.
8. **Finger curl on the free off hand.**
9. **Captions kept in a comfortable band in VR**, with an optional speaker
   direction cue.

### Large

1. **Shift-style quick dash** (SP option).
2. **Fixed foveation**: `XR_FB_foveation` on Quest first (small by itself, but
   it needs a headset), then fragment shading rate and multiview on PC.
3. **Pulling pickups and throwing moveables** (SP). This comes last, because
   Quake 4 gives them little purpose.

## Rejected ideas

| Idea | Why not |
| --- | --- |
| Blink teleport | Skips the triggers a body would touch along the way. Quake 4 levels are built round jumps and continuous movement. In multiplayer it breaks server-owned movement and fairness. Shift keeps the body moving physically and is listed instead. |
| Physical magazines, the backpack and racking | The stock view models have no detachable magazine and no separate reload pieces; reloads are canned animations. Building them needs new models and animations. Gesture reload keeps the feel without content. |
| Wrist pockets, resin, fabricators and upgrades | Quake 4 has no carried consumables or upgrade economy for VR to expose; adding one is new content and a balance change. |
| Multitool and hologram puzzles | New content; Quake 4 has no puzzle spaces. |
| No HUD at all | The stock HUD carries objectives, damage direction and multiplayer state that have no diegetic home in the assets. The wrist split takes the useful part. |
| Physical doors, drawers and levers | Quake 4 doors are scripted movers that open by themselves, and controls are GUIs. Physical versions need content and would break scripted sequences. |
| Melee by swinging | Alyx cut its crowbar because melee without force feedback never felt right and the crowbar snagged on the world. The gauntlet stays on the trigger. |
| Hands only instead of the body | The owner chose a full body (`vr_body 2`), and `vr_body` already offers less. Revisit only if headset reports show the arms read wrongly, which was Valve's reason. |
| Slower or tougher enemies by default | Changes stock behaviour and skill balance, cannot apply in multiplayer, and would make VR saves play differently from flat ones. Measure first (see above). |
| One-handed guns only | Quake 4's guns are rifles in the stock models. openQ4's latched foregrip already avoids the drift that made Valve cut two-handed guns. |
| Liquids, foliage voxels, finger-gesture recognition | No purpose in Quake 4's content or pacing. |

## Candidate roadmap items

These are for the project owner to accept, merge into existing items or
decline. Nothing here is on the roadmap.

| Candidate | Would extend | Scope |
| --- | --- | --- |
| VR fairness and feedback | VR-02 | SP launch clearance and directional and event haptics (both done 27 November); vignette for movers and falls; an HRTF check per headset; light-sensitivity scale. |
| VR access | new, or VR-03 | `vr_moveHand`; single-controller mode; crouch toggle in VR; VR SETUP rows; strings in all 12 languages. |
| VR diegetic status | VR-09 | Wrist HUD trimmed to weapons without their own display; head-locked damage arrows and captions; world-screen and panel fingertip presses; wheel at the hand. |
| VR adaptive quality | VR-04 | Visibility mask; adaptive eye resolution by GPU time; VR anti-aliasing default; Quest foveation under AND-05. |
| VR onboarding | VR-09 | One-time contextual hints. |
| VR interaction (optional) | new | Gesture reload and body quick slots; free-hand finger curl; Shift dash in SP; pointer pickups in SP. |

## Sources

All pages were consulted on 27 November 2026. A note of "snippet" means only a
search-result summary could be read, and "headline only" means the article body
could not be fetched.

| Key | Source |
| --- | --- |
| ow-dc | Combine OverWiki, [Half-Life: Alyx developer commentary transcript][ow-dc] (fan transcript of Valve's in-game commentary) |
| ow-gg | Combine OverWiki, [Gravity Gloves][ow-gg] |
| ow-resin | Combine OverWiki, [Resin][ow-resin] |
| ow-mt | Combine OverWiki, [Multi-tool][ow-mt] |
| rtvr-dc | Road to VR, [Alyx developer commentary update][rtvr-dc] |
| rtvr-walker | Road to VR, [game design interview with Robin Walker][rtvr-walker] |
| rtvr-xr1 | Road to VR, [Inside XR Design: Alyx details, page 1][rtvr-xr1] |
| rtvr-xr2 | Road to VR, [Inside XR Design: Alyx details, page 2][rtvr-xr2] |
| rtvr-rev | Road to VR, [Half-Life: Alyx review][rtvr-rev] |
| rtvr-a11y | Road to VR, [accessibility options before launch][rtvr-a11y] |
| rtvr-11 | Road to VR, [update 1.1: continuous turning][rtvr-11] |
| rtvr-12 | Road to VR, [update 1.2: left-hand options][rtvr-12] |
| rtvr-14 | Road to VR, [update 1.4: liquid, subtitles, scripting][rtvr-14] |
| rtvr-comfort | Road to VR, [VR comfort settings checklist][rtvr-comfort] |
| uvr-int | UploadVR, [Valve interview on combat, AI and access][uvr-int] |
| uvr-loco | UploadVR, [Valve's locomotion deep dive][uvr-loco] |
| uvr-tele | UploadVR, [why Alyx's teleport counts as movement][uvr-tele] |
| uvr-ama | UploadVR, [Valve's Alyx AMA][uvr-ama] |
| uvr-rev | UploadVR, [Half-Life: Alyx review][uvr-rev] |
| uvr-bone | UploadVR, [Alyx and Boneworks physics][uvr-bone] |
| uvr-index | UploadVR, [Index-only interactions][uvr-index] |
| uvr-1hand | UploadVR, [single-controller mode][uvr-1hand] |
| uvr-crates | UploadVR, [wrist pockets and the crate workaround][uvr-crates] |
| gd-loco | Game Developer, [how height, audio cues and trust shaped Alyx's locomotion][gd-loco] |
| gi-arms | Game Informer, [Valve on why arms don't work in VR][gi-arms] |
| engadget | Engadget, [Valve interview on gloves and doors][engadget] |
| gr-walker | Valve Archive copy of Gamereactor's [Robin Walker interview][gr-walker] |
| vb-ama | VentureBeat, [what the Alyx AMA revealed][vb-ama] |
| kotaku-rev | Kotaku, [Half-Life: Alyx review][kotaku-rev] |
| kotaku-crow | Kotaku, [why there is no crowbar][kotaku-crow] |
| pcgn-tall | PCGamesN, [tall testers and the movement system][pcgn-tall] |
| pcg-fast | PC Gamer, [fast zombies cut from VR][pcg-fast] (headline and excerpt) |
| pcg-arms | PC Gamer, [the case for arms][pcg-arms] (headline only) |
| tc | TechCrunch, [Half-Life: Alyx review][tc] |
| cipt | CanIPlayThat, [deaf and hard-of-hearing review][cipt] |
| ase | A Sound Effect, [interview with Alyx's sound team][ase] |
| gs-a11y | GameSpot, [accessibility options guide][gs-a11y] (snippet) |
| wiki | Wikipedia, [Half-Life: Alyx][wiki] |
| steam | Steam, [store page and system requirements][steam] |
| vdc | Valve Developer Community, [Source 2][vdc] |
| pcgw | PCGamingWiki, [Half-Life: Alyx][pcgw] (snippet) |
| steam-drs | Steam Community, [dynamic resolution discussion][steam-drs] |
| vlachos15 | Alex Vlachos, [Advanced VR Rendering, GDC 2015 (slides)][vlachos15] |
| vlachos16 | Alex Vlachos, [Advanced VR Rendering Performance, GDC 2016 (slides)][vlachos16] |

[ow-dc]: https://combineoverwiki.net/wiki/Developer_commentary/Half-Life:_Alyx
[ow-gg]: https://combineoverwiki.net/wiki/Gravity_Gloves
[ow-resin]: https://combineoverwiki.net/wiki/Resin
[ow-mt]: https://combineoverwiki.net/wiki/Multi-tool
[rtvr-dc]: https://roadtovr.com/half-life-alyx-vr-developer-commentary/
[rtvr-walker]: https://roadtovr.com/valve-half-life-alyx-game-design-interview-robin-walker/
[rtvr-xr1]: https://www.roadtovr.com/these-details-make-half-life-alyx-unlike-any-other-vr-game-inside-xr-design/
[rtvr-xr2]: https://www.roadtovr.com/these-details-make-half-life-alyx-unlike-any-other-vr-game-inside-xr-design/2/
[rtvr-rev]: https://www.roadtovr.com/half-life-alyx-review/
[rtvr-a11y]: https://roadtovr.com/half-life-alyx-accessibility-options-subtitles-seated-single-controller/
[rtvr-11]: https://roadtovr.com/half-life-alyx-continuous-turning-smooth-crash-fix/
[rtvr-12]: https://roadtovr.com/half-life-alyx-update-1-2-left-hand-options/
[rtvr-14]: https://www.roadtovr.com/half-life-alyx-update-1-4-liquid-simulation-subtitles-mod-scripting-lua/
[rtvr-comfort]: https://roadtovr.com/vr-comfort-settings-checklist-glossary-developers-players/
[uvr-int]: https://uploadvr.com/valve-interview-half-life-alyx/
[uvr-loco]: https://uploadvr.com/valve-deep-dive-locomotion/
[uvr-tele]: https://uploadvr.com/half-life-alyx-teleporting/
[uvr-ama]: https://www.uploadvr.com/half-life-alyx-ama/
[uvr-rev]: https://www.uploadvr.com/half-life-alyx-review/
[uvr-bone]: https://uploadvr.com/half-life-alyx-boneworks-physics/
[uvr-index]: https://uploadvr.com/valve-index-alyx-interactions/
[uvr-1hand]: https://uploadvr.com/half-life-alyx-one-hand/
[uvr-crates]: https://www.uploadvr.com/valve-half-life-alyx-crates/
[gd-loco]: https://gamedeveloper.com/design/how-height-audio-cues-and-trust-sculpted-vr-locomotion-in-i-half-life-alyx-i-
[gi-arms]: https://www.gameinformer.com/index.php/interview/2020/03/23/valve-talks-half-life-alyx-and-why-arms-dont-work-in-vr
[engadget]: https://www.engadget.com/2020-03-31-half-life-alyx-valve-interview-vr.html
[gr-walker]: https://www.valvearchive.com/archive/Half-Life/Half-Life%20Alyx/Press%20Coverage/Web/2020-03-23%20-%20Between%20Nostalgia%20and%20Novelty%20The%20Half%20Life%20Interview/int.txt
[vb-ama]: https://venturebeat.com/business/half-life-alyx-ama-everything-we-learned
[kotaku-rev]: https://kotaku.com/half-life-alyx-the-kotaku-review-1842446543
[kotaku-crow]: https://kotaku.com/why-you-cant-use-a-crowbar-in-half-life-alyx-1842491319
[pcgn-tall]: https://www.pcgamesn.com/half-life-alyx/vr-bugs
[pcg-fast]: https://www.pcgamer.com/valve-made-half-life-alyx-less-scary-because-fast-zombies-in-vr-arent-fun
[pcg-arms]: https://www.pcgamer.com/au/alyx-needs-arms
[tc]: https://techcrunch.com/2020/03/23/half-lifealyx-delivers-the-watershed-moment-vr-gaming-needs/
[cipt]: https://caniplaythat.com/2020/03/25/deaf-hoh-review-half-life-alyx/
[ase]: https://www.asoundeffect.com/half-life-alyx-vr-sound/
[gs-a11y]: https://www.gamespot.com/articles/half-life-alyx-accessibility-options-guide/1100-6475045/
[wiki]: https://en.wikipedia.org/wiki/Half-Life:_Alyx
[steam]: https://store.steampowered.com/app/546560/HalfLife_Alyx/
[vdc]: https://developer.valvesoftware.com/wiki/Source_2
[pcgw]: https://www.pcgamingwiki.com/wiki/Half-Life:_Alyx
[steam-drs]: https://steamcommunity.com/app/546560/discussions/0/2143091644397121498/
[vlachos15]: https://media.steampowered.com/apps/valve/2015/Alex_Vlachos_Advanced_VR_Rendering_GDC2015.pdf
[vlachos16]: https://www.slideshare.net/slideshow/alexvlachosadvancedvrrenderingperformancegdc2016/59820360
